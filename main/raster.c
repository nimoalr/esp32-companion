#include "raster.h"

#include <math.h>
#include <string.h>
#include "esp_attr.h"

typedef struct {
    int32_t l, r;   /* [l, r) in Q16 screen x */
} span_t;

/* Four sub-scanlines per row at y + 1/8, 3/8, 5/8, 7/8; weights sum to 255. */
static const DRAM_ATTR int32_t k_sub_off[4] = { 8192, 24576, 40960, 57344 };
static const DRAM_ATTR uint8_t k_sub_w[4] = { 64, 64, 64, 63 };

/* Slopes/bulges below 1/256 px are treated as zero so their reciprocals stay in range. */
#define RCP_MIN_Q16  256
#define X_CLAMP_Q16  (1 << 30)

/* Row-local coverage accumulator (band width max). */
static uint8_t s_cov[512];

static inline uint32_t IRAM_ATTR isqrt32(uint32_t v)
{
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* sqrt of a Q16 value, result in Q16 (1/256 px resolution). */
static inline int32_t IRAM_ATTR sqrt_q16(uint32_t v)
{
    return (int32_t)(isqrt32(v) << 8);
}

/* Span-list edits. Lists hold at most RASTER_MAX_SPANS intervals, sorted and disjoint. */
#define RASTER_MAX_SPANS 4

/* Remove the open interval (a, b) from the list. */
static inline int IRAM_ATTR spans_remove(span_t *sp, int n, int32_t a, int32_t b)
{
    if (a >= b) {
        return n;
    }
    span_t out[RASTER_MAX_SPANS];
    int m = 0;
    for (int i = 0; i < n; i++) {
        const int32_t l = sp[i].l, r = sp[i].r;
        if (b <= l || a >= r) {
            out[m++] = sp[i];
            continue;
        }
        if (l < a && m < RASTER_MAX_SPANS) { out[m].l = l; out[m].r = a; m++; }
        if (b < r && m < RASTER_MAX_SPANS) { out[m].l = b; out[m].r = r; m++; }
    }
    for (int i = 0; i < m; i++) {
        sp[i] = out[i];
    }
    return m;
}

/* Intersect the list with [a, b). */
static inline int IRAM_ATTR spans_keep(span_t *sp, int n, int32_t a, int32_t b)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        int32_t l = sp[i].l > a ? sp[i].l : a;
        int32_t r = sp[i].r < b ? sp[i].r : b;
        if (l < r) {
            sp[m].l = l;
            sp[m].r = r;
            m++;
        }
    }
    return m;
}

static inline int32_t IRAM_ATTR to_q16x(const raster_shape_t *s, float x)
{
    if (x > 8000.f) x = 8000.f;
    if (x < -8000.f) x = -8000.f;
    return s->cx + (int32_t)(x * 65536.f);
}

/*
 * Keep the part of the list where A x^2 + B x + C >= 0, x relative to cx (float px).
 * Used for the bent lids: both lid curves reduce to one such inequality per sub-row.
 */
static inline int IRAM_ATTR spans_quad_keep(const raster_shape_t *s, span_t *sp, int n, float A, float B, float C)
{
    if (A > -1e-7f && A < 1e-7f) {
        if (B > -1e-7f && B < 1e-7f) {
            return C >= 0.f ? n : 0;
        }
        const int32_t x = to_q16x(s, -C / B);
        return B > 0.f ? spans_keep(sp, n, x, INT32_MAX) : spans_keep(sp, n, INT32_MIN, x);
    }
    const float D = B * B - 4.f * A * C;
    if (D <= 0.f) {
        return A > 0.f ? n : 0;          /* no roots: the sign is A's everywhere */
    }
    const float sq = sqrtf(D);
    const float r0 = (-B - sq) / (2.f * A), r1 = (-B + sq) / (2.f * A);
    const int32_t X0 = to_q16x(s, r0 < r1 ? r0 : r1), X1 = to_q16x(s, r0 < r1 ? r1 : r0);
    if (A > 0.f) {
        return spans_remove(sp, n, X0, X1);     /* positive outside the roots */
    }
    return spans_keep(sp, n, X0, X1);           /* positive between the roots */
}

/* Top lid with a bend, upright shape: visible where y >= top_base + slant*u + bend*(1 - u^2/hw^2), u = x - cx. */
static inline int IRAM_ATTR top_bend_upright(const raster_shape_t *s, int32_t y, span_t *sp, int n)
{
    /* y - lid(u) >= 0  <=>  (bend/hw^2) u^2 - slant u + (y - top_base - bend) >= 0 */
    const float Y = (float)(y - s->cy) * (1.f / 65536.f);
    return spans_quad_keep(s, sp, n, s->fbend * s->finv_hw2, -s->fslant, Y - s->ftop - s->fbend);
}

/*
 * Visible x-intervals of shape `s` on the sub-scanline at y (Q16).
 * Returns the number of spans written to out[] (up to RASTER_MAX_SPANS).
 */
static inline int IRAM_ATTR shape_spans(const raster_shape_t *s, int32_t y, span_t *out)
{
    const int32_t sdy = y - s->cy;
    const int32_t dy = sdy < 0 ? -sdy : sdy;
    if (dy >= s->hh) {
        return 0;
    }

    /* Rounded corners: the row's two corners can have different radii. */
    const int32_t *rad = &s->rad[sdy < 0 ? RAD_TL : RAD_BL];
    int32_t half[2];
    const int nk = rad[0] == rad[1] ? 1 : 2;      /* one square root when the row's corners match */
    for (int k = 0; k < nk; k++) {
        const int32_t r = rad[k];
        const int32_t inner = s->hh - r;
        if (dy <= inner) {
            half[k] = s->hw;
        } else {
            const int32_t d = dy - inner;                              /* < r, since dy < hh */
            const int64_t rr = (int64_t)r * r - (int64_t)d * d;        /* Q32, > 0 */
            half[k] = (s->hw - r) + sqrt_q16((uint32_t)(rr >> 16));
        }
    }
    if (nk == 1) {
        half[1] = half[0];
    }
    int32_t xl = s->cx - half[0];
    int32_t xr = s->cx + half[1];

    /* Straight top lid: visible where y >= top_base + slant * (x - cx). */
    const int32_t dt = y - s->top_base;
    if (s->bend == 0) {
        if (s->slant == 0) {
            if (dt < 0) {
                return 0;
            }
        } else {
            int64_t xc64 = (int64_t)s->cx + (((int64_t)dt * s->slant_rcp) >> 16);
            if (xc64 > X_CLAMP_Q16) xc64 = X_CLAMP_Q16;
            if (xc64 < -X_CLAMP_Q16) xc64 = -X_CLAMP_Q16;
            const int32_t xc = (int32_t)xc64;
            if (s->slant > 0) {
                if (xc < xr) {
                    xr = xc;
                }
            } else {
                if (xc > xl) {
                    xl = xc;
                }
            }
        }
    }
    if (xl >= xr) {
        return 0;
    }

    /* Bottom lid, straight part. */
    const int32_t db = s->bot_base - y;
    if (db < 0) {
        return 0;
    }

    out[0].l = xl;
    out[0].r = xr;
    int n = 1;
    if (s->bend != 0) {
        n = top_bend_upright(s, y, out, n);
        if (n == 0) {
            return 0;
        }
    }
    /* Bottom arc: visible where y <= bot_base - curve * (1 - t^2), t = (x - cx) / hw. */
    if (s->curve > 0 && db < s->curve) {
        /* t^2 >= 1 - db / curve  ->  |x - cx| >= hw * sqrt(q) */
        int32_t q = Q16_ONE - (int32_t)(((int64_t)db * s->curve_rcp) >> 16);
        if (q < 0) {
            q = 0;
        }
        const int32_t g = (int32_t)(((int64_t)s->hw * sqrt_q16((uint32_t)q)) >> 16);
        n = spans_remove(out, n, s->cx - g, s->cx + g);
    }
    return n;
}

/*
 * Rotated shapes. A horizontal scanline through a rotated rounded rectangle
 * is still one interval (the shape is convex, also with unequal corner
 * radii): the rounded rectangle is the set of points within r of the core
 * rectangle [-a, a] x [-b, b] (local frame), and along the line the local
 * coordinates are linear in screen x, so each of the nine Voronoi regions of
 * the core contributes an interval that is linear (edge regions) or quadratic
 * with unit leading coefficient (corner regions). With unequal radii the line
 * is split at the local axes and each quadrant uses its own radius. Lids stay
 * half-planes, the bent lids are quadratics. Float per sub-row, never per
 * pixel: the spans feed the same exact-coverage code as the upright path.
 */
#define FINF 3.0e38f

/* Intervals of screen x (relative to the centre) where k*x + off is below -h, within [-h, h], above h. */
static inline void IRAM_ATTR zones3(float k, float off, float h, float z[3][2])
{
    if (k > -1e-6f && k < 1e-6f) {
        for (int i = 0; i < 3; i++) { z[i][0] = FINF; z[i][1] = -FINF; }
        const int i = off < -h ? 0 : (off > h ? 2 : 1);
        z[i][0] = -FINF; z[i][1] = FINF;
        return;
    }
    const float x1 = (-h - off) / k, x2 = (h - off) / k;
    if (k > 0.f) {
        z[0][0] = -FINF; z[0][1] = x1;
        z[1][0] = x1;    z[1][1] = x2;
        z[2][0] = x2;    z[2][1] = FINF;
    } else {
        z[0][0] = x1;    z[0][1] = FINF;
        z[1][0] = x2;    z[1][1] = x1;
        z[2][0] = -FINF; z[2][1] = x2;
    }
}

/* Screen-x interval where -h <= k*x + off <= h. Empty when lo >= hi. */
static inline void IRAM_ATTR band3(float k, float off, float h, float *lo, float *hi)
{
    if (k > -1e-6f && k < 1e-6f) {
        if (off >= -h && off <= h) { *lo = -FINF; *hi = FINF; } else { *lo = FINF; *hi = -FINF; }
        return;
    }
    const float x1 = (-h - off) / k, x2 = (h - off) / k;
    if (x1 < x2) { *lo = x1; *hi = x2; } else { *lo = x2; *hi = x1; }
}

/*
 * Interval of the line (lx = c*x + p, ly = -sn*x + q) inside the rounded rectangle of
 * half sizes (hw, hh) and corner radius r, restricted to screen x in [xlo, xhi].
 * Widens [*lo, *hi].
 */
static inline __attribute__((always_inline)) void IRAM_ATTR rr_interval(float c, float sn, float p, float q, float hw, float hh, float r,
                                         bool clip, float xlo, float xhi, float *lo, float *hi)
{
    const float a = hw - r, b = hh - r;
    float xz[3][2], yz[3][2];
    zones3(c, p, a, xz);
    zones3(-sn, q, b, yz);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float rlo = xz[i][0] > yz[j][0] ? xz[i][0] : yz[j][0];
            float rhi = xz[i][1] < yz[j][1] ? xz[i][1] : yz[j][1];
            if (clip) {
                if (rlo < xlo) rlo = xlo;
                if (rhi > xhi) rhi = xhi;
            }
            if (rlo >= rhi) continue;
            float clo, chi;
            if (i == 1 && j == 1) {
                clo = -FINF; chi = FINF;
            } else if (i == 1) {
                band3(-sn, q, hh, &clo, &chi);
            } else if (j == 1) {
                band3(c, p, hw, &clo, &chi);
            } else {
                const float sx = (i == 0) ? -1.f : 1.f, sy = (j == 0) ? -1.f : 1.f;
                const float pp = p - sx * a, qq = q - sy * b;   /* u = c*x + pp, v = -sn*x + qq */
                const float beta = c * pp - sn * qq;             /* x^2 + 2*beta*x + gamma <= 0 */
                const float gamma = pp * pp + qq * qq - r * r;
                const float D = beta * beta - gamma;
                if (D <= 0.f) continue;
                const float sq = sqrtf(D);
                clo = -beta - sq;
                chi = -beta + sq;
            }
            if (clo > rlo) rlo = clo;
            if (chi < rhi) rhi = chi;
            if (rlo < rhi) {
                if (rlo < *lo) *lo = rlo;
                if (rhi > *hi) *hi = rhi;
            }
        }
    }
}

static inline int IRAM_ATTR shape_spans_rot(const raster_shape_t *s, int32_t y, span_t *out)
{
    const float Y = (float)(y - s->cy) * (1.f / 65536.f);
    const float c = s->fc, sn = s->fs;
    const float p = sn * Y, q = c * Y;               /* lx = c*x + p, ly = -sn*x + q */
    float lo = FINF, hi = -FINF;

    if (s->rad_equal) {
        rr_interval(c, sn, p, q, s->fhw, s->fhh, s->frad[0], false, -FINF, FINF, &lo, &hi);
    } else {
        /* split the line where it crosses the local axes; each piece lies in one quadrant */
        float cut[2];
        int nc = 0;
        if (c > 1e-6f || c < -1e-6f) cut[nc++] = -p / c;
        if (sn > 1e-6f || sn < -1e-6f) cut[nc++] = q / sn;
        if (nc == 2 && cut[0] > cut[1]) { const float t = cut[0]; cut[0] = cut[1]; cut[1] = t; }
        for (int k = 0; k <= nc; k++) {
            const float xlo = k == 0 ? -FINF : cut[k - 1];
            const float xhi = k == nc ? FINF : cut[k];
            if (xlo >= xhi) continue;
            const float xm = (k == 0) ? (nc ? cut[0] - 1.f : 0.f) : (k == nc ? cut[nc - 1] + 1.f : 0.5f * (xlo + xhi));
            const float lx = c * xm + p, ly = -sn * xm + q;
            const int qi = (lx >= 0.f ? 1 : 0) + (ly >= 0.f ? 2 : 0);
            rr_interval(c, sn, p, q, s->fhw, s->fhh, s->frad[qi], true, xlo, xhi, &lo, &hi);
        }
    }
    if (lo >= hi) return 0;

    /* top lid, straight: keep ly >= top + slant*lx  ->  (-sn - slant*c) x >= top + slant*p - q */
    if (s->bend == 0) {
        const float k = -sn - s->fslant * c, m = s->ftop + s->fslant * p - q;
        if (k > -1e-6f && k < 1e-6f) {
            if (m > 0.f) return 0;
        } else {
            const float x = m / k;
            if (k > 0.f) { if (x > lo) lo = x; } else { if (x < hi) hi = x; }
        }
    }
    /* bottom lid, straight part: keep ly <= bot  ->  -sn x <= bot - q */
    {
        const float k = -sn, m = s->fbot - q;
        if (k > -1e-6f && k < 1e-6f) {
            if (m < 0.f) return 0;
        } else {
            const float x = m / k;
            if (k > 0.f) { if (x < hi) hi = x; } else { if (x > lo) lo = x; }
        }
    }
    if (lo >= hi) return 0;

    const int32_t L = to_q16x(s, lo), R = to_q16x(s, hi);
    if (L >= R) return 0;
    out[0].l = L; out[0].r = R;
    int n = 1;
    const float ih2 = s->finv_hw2;
    if (s->bend != 0) {
        /* keep ly - top - slant*lx - bend*(1 - lx^2/hw^2) >= 0 */
        const float bd = s->fbend * ih2;
        const float A = bd * c * c;
        const float B = 2.f * bd * c * p - sn - s->fslant * c;
        const float C = bd * p * p + q - s->ftop - s->fslant * p - s->fbend;
        n = spans_quad_keep(s, out, n, A, B, C);
        if (n == 0) return 0;
    }
    if (s->fcurve > 0.f) {
        /* keep bot - curve*(1 - lx^2/hw^2) - ly >= 0 */
        const float cv = s->fcurve;
        const float A = cv * c * c * ih2;
        const float B = 2.f * cv * c * p * ih2 + sn;
        const float C = cv * p * p * ih2 - q + s->fbot - cv;
        n = spans_quad_keep(s, out, n, A, B, C);
    }
    return n;
}

/* Add weighted coverage of [l, r) into cov[], which covers pixels [base, base + n). */
static inline void IRAM_ATTR cov_add(uint8_t *cov, int base, int n, int32_t l, int32_t r, int32_t w)
{
    const int32_t lim_l = base << 16;
    const int32_t lim_r = (base + n) << 16;
    if (l < lim_l) {
        l = lim_l;
    }
    if (r > lim_r) {
        r = lim_r;
    }
    if (l >= r) {
        return;
    }
    const int il = l >> 16;
    const int ir = (r - 1) >> 16;
    if (il == ir) {
        cov[il - base] += (uint8_t)(((r - l) * w) >> 16);
        return;
    }
    cov[il - base] += (uint8_t)(((((il + 1) << 16) - l) * w) >> 16);
    for (int x = il + 1; x < ir; x++) {
        cov[x - base] += (uint8_t)w;
    }
    cov[ir - base] += (uint8_t)(((r - (ir << 16)) * w) >> 16);
}

static bool s_over;   /* raster_shapes_over(): leave uncovered pixels alone */

/* Blend the colour (byte-swapped RGB565) over dst with alpha a (0..255). */
static inline uint16_t IRAM_ATTR blend565(uint16_t dst, uint16_t col, uint32_t a)
{
    const uint32_t d = (uint32_t)((dst >> 8) | (dst << 8)) & 0xFFFF;
    const uint32_t c = (uint32_t)((col >> 8) | (col << 8)) & 0xFFFF;
    const uint32_t ia = 256 - (a + 1);
    a += 1;
    const uint32_t r = (((c >> 11) & 0x1F) * a + ((d >> 11) & 0x1F) * ia) >> 8;
    const uint32_t g = (((c >> 5) & 0x3F) * a + ((d >> 5) & 0x3F) * ia) >> 8;
    const uint32_t b = ((c & 0x1F) * a + (d & 0x1F) * ia) >> 8;
    const uint32_t o = (r << 11) | (g << 5) | b;
    return (uint16_t)((o >> 8) | (o << 8));
}

static inline void IRAM_ATTR blit_cov(uint16_t *dst, const uint8_t *cov, int n, const uint16_t *lut)
{
    if (s_over) {
        /* edges blend over whatever is there, so overlapping strokes stay solid */
        const uint16_t full = lut[255];
        for (int i = 0; i < n; i++) {
            const uint8_t c = cov[i];
            if (c == 255) dst[i] = full;
            else if (c) dst[i] = dst[i] ? blend565(dst[i], full, c) : lut[c];
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        dst[i] = lut[cov[i]];
    }
}

static inline void IRAM_ATTR fill16(uint16_t *p, uint16_t v, int n)
{
    if (n <= 0) {
        return;
    }
    if ((uintptr_t)p & 2) {
        *p++ = v;
        n--;
    }
    const uint32_t vv = (uint32_t)v | ((uint32_t)v << 16);
    uint32_t *p32 = (uint32_t *)p;
    for (int i = n >> 1; i > 0; i--) {
        *p32++ = vv;
    }
    if (n & 1) {
        *(uint16_t *)p32 = v;
    }
}

/* Render one shape into one row. `row` covers screen x in [bx0, bx1). */
static void IRAM_ATTR render_row(uint16_t *row, int bx0, int bx1, int py, const raster_shape_t *s)
{
    span_t sp[4][RASTER_MAX_SPANS];
    int ns[4];
    int total = 0;
    bool multi = false;
    const int32_t ybase = py << 16;

    for (int k = 0; k < 4; k++) {
        ns[k] = s->rot ? shape_spans_rot(s, ybase + k_sub_off[k], sp[k]) : shape_spans(s, ybase + k_sub_off[k], sp[k]);
        total += ns[k];
        if (ns[k] > 1) {
            multi = true;
        }
    }
    if (total == 0) {
        return;
    }

    const uint16_t *lut = s->lut;

    if (!multi) {
        /* Single-span sub-rows: exact edges around a solid core. */
        int32_t L = INT32_MAX, R = INT32_MIN;      /* outer extent */
        int32_t CL = INT32_MIN, CR = INT32_MAX;    /* extent covered by every sub-row */
        int cnt = 0;
        for (int k = 0; k < 4; k++) {
            if (ns[k]) {
                const int32_t l = sp[k][0].l, r = sp[k][0].r;
                if (l < L) L = l;
                if (r > R) R = r;
                if (l > CL) CL = l;
                if (r < CR) CR = r;
                cnt++;
            }
        }
        int pl = L >> 16;
        int pr = ((R - 1) >> 16) + 1;
        if (pl < bx0) pl = bx0;
        if (pr > bx1) pr = bx1;
        if (pl >= pr) {
            return;
        }

        int cl = pl, cr = pl;   /* solid core [cl, cr), empty unless all four sub-rows present */
        if (cnt == 4) {
            cl = (CL + 0xFFFF) >> 16;
            cr = CR >> 16;
            if (cl < pl) cl = pl;
            if (cr > pr) cr = pr;
            if (cl >= cr) {
                cl = cr = pl;
            }
        }

        if (cl > pl) {
            const int n = cl - pl;
            memset(s_cov, 0, (size_t)n);
            for (int k = 0; k < 4; k++) {
                if (ns[k]) cov_add(s_cov, pl, n, sp[k][0].l, sp[k][0].r, k_sub_w[k]);
            }
            blit_cov(row + (pl - bx0), s_cov, n, lut);
        }
        if (cr > cl) {
            fill16(row + (cl - bx0), lut[255], cr - cl);
        }
        if (pr > cr) {
            const int n = pr - cr;
            memset(s_cov, 0, (size_t)n);
            for (int k = 0; k < 4; k++) {
                if (ns[k]) cov_add(s_cov, cr, n, sp[k][0].l, sp[k][0].r, k_sub_w[k]);
            }
            blit_cov(row + (cr - bx0), s_cov, n, lut);
        }
        return;
    }

    /* General path (bottom arc splits the row): accumulate over the union extent. */
    int32_t L = INT32_MAX, R = INT32_MIN;
    for (int k = 0; k < 4; k++) {
        for (int j = 0; j < ns[k]; j++) {
            if (sp[k][j].l < L) L = sp[k][j].l;
            if (sp[k][j].r > R) R = sp[k][j].r;
        }
    }
    int pl = L >> 16;
    int pr = ((R - 1) >> 16) + 1;
    if (pl < bx0) pl = bx0;
    if (pr > bx1) pr = bx1;
    if (pl >= pr) {
        return;
    }
    const int n = pr - pl;
    memset(s_cov, 0, (size_t)n);
    for (int k = 0; k < 4; k++) {
        for (int j = 0; j < ns[k]; j++) {
            cov_add(s_cov, pl, n, sp[k][j].l, sp[k][j].r, k_sub_w[k]);
        }
    }
    blit_cov(row + (pl - bx0), s_cov, n, lut);
}

static void IRAM_ATTR draw_shapes(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes)
{
    const int x1 = x0 + w;
    for (int i = 0; i < nshapes; i++) {
        const raster_shape_t *s = &shapes[i];
        if (!s->visible || s->px1 <= x0 || s->px0 >= x1) {
            continue;
        }
        int ya = s->py0 > y0 ? s->py0 : y0;
        int yb = s->py1 < y0 + rows ? s->py1 : y0 + rows;
        for (int py = ya; py < yb; py++) {
            render_row(dst + (size_t)(py - y0) * w, x0, x1, py, s);
        }
    }
}

void IRAM_ATTR raster_band(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes)
{
    memset(dst, 0, (size_t)w * (size_t)rows * 2u);
    s_over = false;
    draw_shapes(dst, x0, y0, w, rows, shapes, nshapes);
}

void raster_shapes_over(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes)
{
    s_over = true;
    draw_shapes(dst, x0, y0, w, rows, shapes, nshapes);
    s_over = false;
}

void raster_shape_finalize(raster_shape_t *s, int screen_w, int screen_h)
{
    if (s->slant > -RCP_MIN_Q16 && s->slant < RCP_MIN_Q16) s->slant = 0;
    if (s->curve < RCP_MIN_Q16) s->curve = 0;
    if (s->bend > -RCP_MIN_Q16 && s->bend < RCP_MIN_Q16) s->bend = 0;
    s->slant_rcp = s->slant ? (int32_t)((1LL << 32) / s->slant) : 0;
    s->curve_rcp = s->curve ? (int32_t)((1LL << 32) / s->curve) : 0;

    if (s->hw <= 0 || s->hh <= 0) {
        s->visible = false;
        return;
    }
    const int32_t rmax = s->hw < s->hh ? s->hw : s->hh;
    for (int i = 0; i < 4; i++) {
        if (s->rad[i] < 0) s->rad[i] = 0;
        if (s->rad[i] > rmax) s->rad[i] = rmax;
    }
    s->rad_equal = s->rad[0] == s->rad[1] && s->rad[0] == s->rad[2] && s->rad[0] == s->rad[3];

    /* Vertical extent after the lids: the top line is lowest at |x - cx| = hw, an arched lid rises by |bend|. */
    int32_t aslant = s->slant < 0 ? -s->slant : s->slant;
    int32_t top = s->top_base - (int32_t)(((int64_t)aslant * s->hw) >> 16);
    if (s->bend < 0) top += s->bend;
    if (top < s->cy - s->hh) top = s->cy - s->hh;
    int32_t bot = s->bot_base;
    if (bot > s->cy + s->hh) bot = s->cy + s->hh;
    if (top >= bot) {
        s->visible = false;
        return;
    }
    if (s->rot || s->bend != 0) {
        const float k = 1.f / 65536.f;
        s->fc = (float)s->rc * k;
        s->fs = (float)s->rs * k;
        s->fhw = (float)s->hw * k;
        s->fhh = (float)s->hh * k;
        for (int i = 0; i < 4; i++) s->frad[i] = (float)s->rad[i] * k;
        s->ftop = (float)(s->top_base - s->cy) * k;
        s->fbot = (float)(s->bot_base - s->cy) * k;
        s->fslant = (float)s->slant * k;
        s->fcurve = (float)s->curve * k;
        s->fbend = (float)s->bend * k;
        s->finv_hw2 = s->fhw > 0.f ? 1.f / (s->fhw * s->fhw) : 0.f;
    }
    int x0, x1, y0, y1;
    if (s->rot) {
        /* rotate the local bbox corners about the centre */
        const int32_t lx[2] = { -s->hw, s->hw };
        const int32_t ly[2] = { top - s->cy, bot - s->cy };
        int32_t minx = INT32_MAX, maxx = INT32_MIN, miny = INT32_MAX, maxy = INT32_MIN;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                const int32_t X = s->cx + (int32_t)(((int64_t)lx[i] * s->rc - (int64_t)ly[j] * s->rs) >> 16);
                const int32_t Y = s->cy + (int32_t)(((int64_t)lx[i] * s->rs + (int64_t)ly[j] * s->rc) >> 16);
                if (X < minx) minx = X;
                if (X > maxx) maxx = X;
                if (Y < miny) miny = Y;
                if (Y > maxy) maxy = Y;
            }
        }
        x0 = (minx >> 16) - 1;
        x1 = ((maxx + 0xFFFF) >> 16) + 2;
        y0 = (miny >> 16) - 1;
        y1 = ((maxy + 0xFFFF) >> 16) + 2;
    } else {
        x0 = (s->cx - s->hw) >> 16;
        x1 = ((s->cx + s->hw + 0xFFFF) >> 16) + 1;
        y0 = top >> 16;
        y1 = ((bot + 0xFFFF) >> 16) + 1;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > screen_w) x1 = screen_w;
    if (y1 > screen_h) y1 = screen_h;
    s->px0 = x0; s->px1 = x1; s->py0 = y0; s->py1 = y1;
    s->visible = (x0 < x1) && (y0 < y1);
}

void raster_build_lut(uint16_t lut[256], uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 256; i++) {
        const uint32_t rr = (r * i + 127) / 255;
        const uint32_t gg = (g * i + 127) / 255;
        const uint32_t bb = (b * i + 127) / 255;
        const uint16_t c = (uint16_t)(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3));
        /* The panel receives the high byte first; store big-endian in memory. */
        lut[i] = (uint16_t)((c >> 8) | (c << 8));
    }
}
