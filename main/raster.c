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

/* Row-local coverage accumulator (band width max): lives on the caller's stack so two cores can raster at once. */
#define COV_MAX 512

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
#define RASTER_MAX_SPANS 8

bool raster_path_add(raster_shape_t *s, const int32_t (*xy)[2], int n)
{
    if (n < 3 || n + s->path_n > RASTER_PATH_EDGES) return false;
    if (!s->path_n) {
        s->path_x0 = s->path_y0 = INT32_MAX;
        s->path_x1 = s->path_y1 = INT32_MIN;
    }
    for (int i = 0; i < n; i++) {
        int32_t x0 = xy[i][0], y0 = xy[i][1];
        int32_t x1 = xy[(i+1)%n][0], y1 = xy[(i+1)%n][1];
        if (x0 < s->path_x0) s->path_x0 = x0;
        if (x0 > s->path_x1) s->path_x1 = x0;
        if (y0 < s->path_y0) s->path_y0 = y0;
        if (y0 > s->path_y1) s->path_y1 = y0;
        if (y0 == y1) continue;
        if (y0 > y1) {
            const int32_t x = x0, y = y0; x0 = x1; y0 = y1; x1 = x; y1 = y;
        }
        const int64_t slope = (int64_t)(x1-x0) * Q16_ONE / (y1-y0);
        /* Nearly horizontal edges can have a slope outside Q16 range. Store
         * no more than the useful screen span per 1/65536 px of vertical travel. */
        s->path[s->path_n++] = (raster_edge_t){y0, y1, x0,
            slope > INT32_MAX ? INT32_MAX : slope < INT32_MIN ? INT32_MIN : (int32_t)slope};
    }
    return true;
}

static int IRAM_ATTR path_spans(const raster_shape_t *s, int32_t y, span_t *out)
{
    int32_t cross[2 * RASTER_MAX_SPANS];
    int n = 0;
    for (int i = 0; i < s->path_n; i++) {
        const raster_edge_t *edge = &s->path[i];
        if (y < edge->y0 || y >= edge->y1) continue;
        if (n == 2 * RASTER_MAX_SPANS) break;
        const int32_t x = edge->x0 + (int32_t)(((int64_t)(y-edge->y0)*edge->slope) >> 16);
        int j = n++;
        while (j && cross[j-1] > x) { cross[j] = cross[j-1]; j--; }
        cross[j] = x;
    }
    int count = 0;
    for (int i = 0; i+1 < n; i += 2) {
        if (cross[i] < cross[i+1]) out[count++] = (span_t){cross[i], cross[i+1]};
    }
    return count;
}

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

    /* Rounded corners: the row's two corners can have different (elliptical) radii. */
    const int base = sdy < 0 ? RAD_TL : RAD_BL;
    const int32_t *rad = &s->rad[base], *rady = &s->rady[base], *rxy = &s->rxy[base];
    int32_t half[2];
    const int nk = (rad[0] == rad[1] && rady[0] == rady[1]) ? 1 : 2;   /* one square root when the row's corners match */
    for (int k = 0; k < nk; k++) {
        const int32_t rx = rad[k], ry = rady[k];
        const int32_t inner = s->hh - ry;
        if (dy <= inner) {
            half[k] = s->hw;
        } else {
            const int32_t d = dy - inner;                              /* < ry, since dy < hh */
            const int64_t rr = (int64_t)ry * ry - (int64_t)d * d;      /* Q32, > 0 */
            int32_t sq = sqrt_q16((uint32_t)(rr >> 16));
            if (rx != ry) {
                sq = (int32_t)(((int64_t)sq * rxy[k]) >> 16);          /* ellipse: scale the chord by rx / ry */
            }
            half[k] = (s->hw - rx) + sq;
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

    /* Bottom lid, straight part (plain lids only). */
    const int32_t db = s->bot_base - y;
    if (s->bot_simple && db < 0) {
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
    if (s->bot_simple) {
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
    /* Slanted / sagging bottom lid: visible where y <= bot_base + bslant*u - curve*(1 - u^2/hw^2)
     *  <=>  (curve/hw^2) u^2 + bslant u + (bot_base - curve - y) >= 0 */
    const float Y = (float)(y - s->cy) * (1.f / 65536.f);
    return spans_quad_keep(s, out, n, s->fcurve * s->finv_hw2, s->fbslant, s->fbot - s->fcurve - Y);
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

/* Screen-x range where k*x > m (a ray). */
static inline void IRAM_ATTR ray_gt(float k, float m, float *lo, float *hi)
{
    if (k > 1e-6f) { *lo = m / k; *hi = FINF; }
    else if (k < -1e-6f) { *lo = -FINF; *hi = m / k; }
    else if (m < 0.f) { *lo = -FINF; *hi = FINF; }
    else { *lo = FINF; *hi = -FINF; }
}

/*
 * Walk one end of the line's interval inside the rounded rectangle. `x` is where the line
 * enters the plain rectangle (dir > 0: from the left, dir < 0: from the right). The shape is
 * convex, so the true end is either right there (a flat edge), on the corner ellipse of the
 * corner zone the point sits in, or where the line leaves that corner zone. At most a few
 * steps, instead of testing all nine zone combinations for every sub-row.
 */
static inline float IRAM_ATTR rot_refine_end(const raster_shape_t *s, float c, float sn, float p, float q,
                                             float x, float limit, int dir)
{
    const float hw = s->fhw, hh = s->fhh;
    for (int step = 0; step < 4; step++) {
        const float xe = x + (dir > 0 ? 1e-3f : -1e-3f);          /* just inside the interval */
        const float u = c * xe + p, v = -sn * xe + q;
        const int qi = (u >= 0.f ? 1 : 0) + (v >= 0.f ? 2 : 0);
        const float rx = s->frad[qi], ry = s->frady[qi];
        const float a = hw - rx, b = hh - ry;
        const float su = u >= 0.f ? 1.f : -1.f, sv = v >= 0.f ? 1.f : -1.f;
        if (!(su * u > a && sv * v > b)) {
            return x;                                            /* edge or centre zone: inside */
        }
        /* corner zone: its x range, and the corner ellipse */
        float zl, zr, l2, h2;
        ray_gt(c * su, a - su * p, &zl, &zr);
        ray_gt(-sn * sv, b - sv * q, &l2, &h2);
        if (l2 > zl) zl = l2;
        if (h2 < zr) zr = h2;
        const float pp = p - su * a, qq = q - sv * b;
        const float ix = s->finv_rx2[qi], iy = s->finv_ry2[qi];
        const float A = c * c * ix + sn * sn * iy;
        const float Bh = c * pp * ix - sn * qq * iy;
        const float C = pp * pp * ix + qq * qq * iy - 1.f;
        const float D = Bh * Bh - A * C;
        if (D > 0.f) {
            const float sq = sqrtf(D), ia = 1.f / A;
            const float e0 = (-Bh - sq) * ia, e1 = (-Bh + sq) * ia;
            if (dir > 0) {
                const float cand = e0 > x ? e0 : x;
                if (cand <= e1 && cand <= zr) return cand;
            } else {
                const float cand = e1 < x ? e1 : x;
                if (cand >= e0 && cand >= zl) return cand;
            }
        }
        /* the line misses this corner's arc: leave the zone and look again */
        x = dir > 0 ? zr : zl;
        if (dir > 0 ? x >= limit : x <= limit) return limit;
    }
    return x;
}

static inline int IRAM_ATTR shape_spans_rot(const raster_shape_t *s, int32_t y, span_t *out)
{
    const float Y = (float)(y - s->cy) * (1.f / 65536.f);
    const float c = s->fc, sn = s->fs;
    const float p = sn * Y, q = c * Y;               /* lx = c*x + p, ly = -sn*x + q */

    /* the plain rectangle first: |lx| <= hw and |ly| <= hh */
    float lo, hi, l2, h2;
    band3(c, p, s->fhw, &lo, &hi);
    band3(-sn, q, s->fhh, &l2, &h2);
    if (l2 > lo) lo = l2;
    if (h2 < hi) hi = h2;
    if (lo >= hi) return 0;
    /* then pull each end in over its corner */
    const float lo2 = rot_refine_end(s, c, sn, p, q, lo, hi, +1);
    const float hi2 = rot_refine_end(s, c, sn, p, q, hi, lo2, -1);
    lo = lo2; hi = hi2;
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
    if (s->bot_simple) {
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
    if (!s->bot_simple || s->fcurve > 0.f) {
        /* keep bot + bslant*lx - curve*(1 - lx^2/hw^2) - ly >= 0 */
        const float cv = s->fcurve, bs = s->fbslant;
        const float A = cv * c * c * ih2;
        const float B = 2.f * cv * c * p * ih2 + bs * c + sn;
        const float C = cv * p * p * ih2 + bs * p - q + s->fbot - cv;
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

/* 2x2 ordered dither for the hot spot, in falloff units (one lightness level is about 18). */
static const DRAM_ATTR uint8_t k_dither[4] = { 0, 9, 13, 4 };

/*
 * Spectrum bars, walked along a row in the eye's local frame: lx = c X + s Y, ly = -s X + c Y
 * with (X, Y) relative to the centre, so per pixel lx += c and ly -= s. The bar index follows
 * lx across the bar boundaries in either direction (the face may be upside down).
 */
typedef struct {
    int32_t lx, ly, lo, hi;
    int32_t u,v,du,dv;
    int b;
} bar_walk_t;

static inline void IRAM_ATTR bar_walk_begin(const raster_shape_t *s, int x, int py, bar_walk_t *w)
{
    const int32_t rc = s->rot ? s->rc : Q16_ONE, rs = s->rot ? s->rs : 0;
    const int32_t X = ((int32_t)x << 16) + 0x8000 - s->cx, Y = ((int32_t)py << 16) + 0x8000 - s->cy;
    w->lx = (int32_t)(((int64_t)rc * X + (int64_t)rs * Y) >> 16);
    w->ly = (int32_t)(((int64_t)rc * Y - (int64_t)rs * X) >> 16);
    if(s->fx!=RASTER_FX_BARS) {
        w->u=(int32_t)((int64_t)w->lx*s->fx_sx>>16)+Q16(32);
        w->v=(int32_t)((int64_t)w->ly*s->fx_sy>>16)+Q16(32);
        w->du=(int32_t)((int64_t)rc*s->fx_sx>>16);
        w->dv=-(int32_t)((int64_t)rs*s->fx_sy>>16);
        return;
    }
    int b = (int)(((int64_t)(w->lx + s->hw)) / (s->bar_w > 0 ? s->bar_w : 1));
    if (b < 0) b = 0;
    if (b > 7) b = 7;
    w->b = b;
    w->lo = b * s->bar_w - s->hw;
    w->hi = w->lo + s->bar_w;
}

static inline uint32_t IRAM_ATTR bar_walk_level(const raster_shape_t *s, bar_walk_t *w)
{
    uint32_t level = s->bar_dim;
    if (s->fx == RASTER_FX_BARS) {
        while (w->lx >= w->hi && w->b < 7) { w->b++; w->lo = w->hi; w->hi += s->bar_w; }
        while (w->lx < w->lo && w->b > 0) { w->b--; w->hi = w->lo; w->lo -= s->bar_w; }
        if (!(w->b > 0 && w->lx - w->lo < Q16_ONE)) {          /* the first column of a bar is the gap */
            const int32_t d = w->ly - s->bar_top[w->b];
            if (d >= 0) level = s->bar_lit;
            else if (d > -Q16_ONE) level = s->bar_dim + (uint32_t)(((int64_t)(s->bar_lit - s->bar_dim) * (d + Q16_ONE)) >> 16);
        }
    } else {
        const int u=w->u>>16,v=w->v>>16;
        level=(unsigned)u<64 && (unsigned)v<64 ? s->fx_tex[v*64+u] : 0;
        w->u+=w->du;w->v+=w->dv;
    }
    /* the mix against the plain fill */
    if (s->fx_mix < 256) level = 31 + (((int)level - 31) * s->fx_mix >> 8);
    const int32_t rc = s->rot ? s->rc : Q16_ONE, rs = s->rot ? s->rs : 0;
    w->lx += rc;
    w->ly -= rs;
    return level;
}

/* Solid core of the spectrum bars. */
static inline void IRAM_ATTR fill_bars(uint16_t *dst, int n, const raster_shape_t *s, int x, int py)
{
    const uint16_t *lut2 = s->lut2 + 63;
    bar_walk_t w = {0};
    bar_walk_begin(s, x, py, &w);
    for (int i = 0; i < n; i++) dst[i] = lut2[bar_walk_level(s, &w) << 6];
}

/* Edge pixels: coverage -> colour, optionally shaded by the hot spot. x = screen x of dst[0]. */
static inline void IRAM_ATTR blit_cov(uint16_t *dst, const uint8_t *cov, int n, const raster_shape_t *s, int x, int py, bool over)
{
    const uint16_t *lut = s->lut;
    if (s->fx) {
        const uint16_t *lut2 = s->lut2;
        bar_walk_t w = {0};
        bar_walk_begin(s, x, py, &w);
        for (int i = 0; i < n; i++) {
            const uint32_t level = bar_walk_level(s, &w) << 6;
            if (over && !cov[i]) continue;
            dst[i] = over && dst[i] ? blend565(dst[i], lut2[level | 63], cov[i]) : lut2[level | (cov[i] >> 2)];
        }
        return;
    }
    if (s->hot) {
        const uint8_t *gx = s->hot_gx + x, *g2l = s->hot_g2l;
        const uint32_t gy = s->hot_gy[py];
        const uint16_t *lut2 = s->lut2;
        const uint8_t *dz = &k_dither[(py & 1) * 2];
        for (int i = 0; i < n; i++) {
            const uint32_t level = (uint32_t)g2l[((gx[i] * gy) >> 8) + dz[(x + i) & 1]] << 6;
            if (over && !cov[i]) continue;
            dst[i] = over && dst[i] ? blend565(dst[i], lut2[level | 63], cov[i]) : lut2[level | (cov[i] >> 2)];
        }
        return;
    }
    if (over) {
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

/* Solid core, shaded by the hot spot. */
static inline void IRAM_ATTR fill_hot(uint16_t *dst, int n, const raster_shape_t *s, int x, int py)
{
    const uint8_t *gx = s->hot_gx + x, *g2l = s->hot_g2l;
    const uint32_t gy = s->hot_gy[py];
    const uint16_t *lut2 = s->lut2 + 63;
    const uint8_t *dz = &k_dither[(py & 1) * 2];
    for (int i = 0; i < n; i++) {
        dst[i] = lut2[(uint32_t)g2l[((gx[i] * gy) >> 8) + dz[(x + i) & 1]] << 6];
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
static void IRAM_ATTR render_row(uint16_t *row, int bx0, int bx1, int py, const raster_shape_t *s, bool over)
{
    uint8_t cov[COV_MAX];
    span_t sp[4][RASTER_MAX_SPANS];
    int ns[4];
    int total = 0;
    bool multi = false;
    const int32_t ybase = py << 16;

    for (int k = 0; k < 4; k++) {
        ns[k] = s->path_n ? path_spans(s, ybase + k_sub_off[k], sp[k])
                         : s->rot ? shape_spans_rot(s, ybase + k_sub_off[k], sp[k]) : shape_spans(s, ybase + k_sub_off[k], sp[k]);
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
            memset(cov, 0, (size_t)n);
            for (int k = 0; k < 4; k++) {
                if (ns[k]) cov_add(cov, pl, n, sp[k][0].l, sp[k][0].r, k_sub_w[k]);
            }
            blit_cov(row + (pl - bx0), cov, n, s, pl, py, over);
        }
        if (cr > cl) {
            if (s->fx) {
                fill_bars(row + (cl - bx0), cr - cl, s, cl, py);
            } else if (s->hot) {
                fill_hot(row + (cl - bx0), cr - cl, s, cl, py);
            } else {
                fill16(row + (cl - bx0), lut[255], cr - cl);
            }
        }
        if (pr > cr) {
            const int n = pr - cr;
            memset(cov, 0, (size_t)n);
            for (int k = 0; k < 4; k++) {
                if (ns[k]) cov_add(cov, cr, n, sp[k][0].l, sp[k][0].r, k_sub_w[k]);
            }
            blit_cov(row + (cr - bx0), cov, n, s, cr, py, over);
        }
        return;
    }

    /*
     * General path (a lid curve splits the row into several spans). Pixels inside every
     * sub-row's spans are solid and filled directly; only the pixels around the span ends
     * go through the coverage accumulator. Before, the whole row went through it, which
     * made a curved-lid row about six times the cost of a plain one.
     */
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

    /* solid core: intersection of the four span lists, in whole pixels */
    span_t core_a[2 * RASTER_MAX_SPANS], core_b[2 * RASTER_MAX_SPANS];
    span_t *core = core_a, *tmp = core_b;
    int ncore = 0;
    if (ns[0] && ns[1] && ns[2] && ns[3]) {
        for (int j = 0; j < ns[0]; j++) core[ncore++] = sp[0][j];
        for (int k = 1; k < 4 && ncore; k++) {
            int nt = 0, a = 0, b = 0;
            while (a < ncore && b < ns[k] && nt < 2 * RASTER_MAX_SPANS) {
                const int32_t l = core[a].l > sp[k][b].l ? core[a].l : sp[k][b].l;
                const int32_t r = core[a].r < sp[k][b].r ? core[a].r : sp[k][b].r;
                if (l < r) { tmp[nt].l = l; tmp[nt].r = r; nt++; }
                if (core[a].r < sp[k][b].r) a++; else b++;
            }
            span_t *t = core; core = tmp; tmp = t;
            ncore = nt;
        }
    }

    int x = pl;
    for (int i = 0; i <= ncore; i++) {
        int cl, cr;
        if (i < ncore) {
            cl = (core[i].l + 0xFFFF) >> 16;
            cr = core[i].r >> 16;
            if (cl < pl) cl = pl;
            if (cr > pr) cr = pr;
            if (cl >= cr) continue;
        } else {
            cl = cr = pr;
        }
        /* edge pixels before this core */
        if (cl > x) {
            const int n = cl - x;
            memset(cov, 0, (size_t)n);
            for (int k = 0; k < 4; k++) {
                for (int j = 0; j < ns[k]; j++) {
                    cov_add(cov, x, n, sp[k][j].l, sp[k][j].r, k_sub_w[k]);
                }
            }
            blit_cov(row + (x - bx0), cov, n, s, x, py, over);
        }
        if (cr > cl) {
            if (over) {
                /* the over-paint blend wants coverage for every pixel; keep the exact path */
                const int n = cr - cl;
                memset(cov, 0, (size_t)n);
                for (int k = 0; k < 4; k++) {
                    for (int j = 0; j < ns[k]; j++) {
                        cov_add(cov, cl, n, sp[k][j].l, sp[k][j].r, k_sub_w[k]);
                    }
                }
                blit_cov(row + (cl - bx0), cov, n, s, cl, py, over);
            } else if (s->fx) {
                fill_bars(row + (cl - bx0), cr - cl, s, cl, py);
            } else if (s->hot) {
                fill_hot(row + (cl - bx0), cr - cl, s, cl, py);
            } else {
                fill16(row + (cl - bx0), s->lut[255], cr - cl);
            }
        }
        x = cr > x ? cr : x;
    }
}

static void IRAM_ATTR draw_shapes(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes, bool over)
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
            render_row(dst + (size_t)(py - y0) * w, x0, x1, py, s, over);
        }
    }
}

void IRAM_ATTR raster_band(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes)
{
    memset(dst, 0, (size_t)w * (size_t)rows * 2u);
    draw_shapes(dst, x0, y0, w, rows, shapes, nshapes, false);
}

void raster_shapes_over(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes)
{
    draw_shapes(dst, x0, y0, w, rows, shapes, nshapes, true);
}

void raster_shape_finalize(raster_shape_t *s, int screen_w, int screen_h)
{
    if (s->path_n) {
        int x0 = (s->path_x0 >> 16) - 1, y0 = (s->path_y0 >> 16) - 1;
        int x1 = ((s->path_x1 + 65535) >> 16) + 1, y1 = ((s->path_y1 + 65535) >> 16) + 1;
        s->px0 = x0 < 0 ? 0 : x0; s->py0 = y0 < 0 ? 0 : y0;
        s->px1 = x1 > screen_w ? screen_w : x1; s->py1 = y1 > screen_h ? screen_h : y1;
        s->visible = s->px0 < s->px1 && s->py0 < s->py1;
        return;
    }
    if (s->slant > -RCP_MIN_Q16 && s->slant < RCP_MIN_Q16) s->slant = 0;
    if (s->bend > -RCP_MIN_Q16 && s->bend < RCP_MIN_Q16) s->bend = 0;
    s->slant_rcp = s->slant ? (int32_t)((1LL << 32) / s->slant) : 0;
    s->curve_rcp = s->curve > 0 ? (int32_t)((1LL << 32) / s->curve) : 0;

    if (s->hw <= 0 || s->hh <= 0) {
        s->visible = false;
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (s->rad[i] < 0) s->rad[i] = 0;
        if (s->rad[i] > s->hw) s->rad[i] = s->hw;
        if (s->rady[i] < 0) s->rady[i] = 0;
        if (s->rady[i] > s->hh) s->rady[i] = s->hh;
        s->rxy[i] = (s->rady[i] > 0 && s->rad[i] != s->rady[i]) ? (int32_t)(((int64_t)s->rad[i] << 16) / s->rady[i]) : Q16_ONE;
    }
    s->rad_equal = true;
    for (int i = 0; i < 4; i++) {
        if (s->rad[i] != s->rad[0] || s->rady[i] != s->rady[0]) s->rad_equal = false;
    }
    if (s->bot_slant > -RCP_MIN_Q16 && s->bot_slant < RCP_MIN_Q16) s->bot_slant = 0;
    if (s->curve > -RCP_MIN_Q16 && s->curve < RCP_MIN_Q16) s->curve = 0;
    s->bot_simple = s->bot_slant == 0 && s->curve >= 0;

    /* Vertical extent after the lids: the top line is lowest at |x - cx| = hw, an arched lid rises by |bend|. */
    int32_t aslant = s->slant < 0 ? -s->slant : s->slant;
    int32_t top = s->top_base - (int32_t)(((int64_t)aslant * s->hw) >> 16);
    if (s->bend < 0) top += s->bend;
    if (top < s->cy - s->hh) top = s->cy - s->hh;
    const int32_t abslant = s->bot_slant < 0 ? -s->bot_slant : s->bot_slant;
    int32_t bot = s->bot_base + (int32_t)(((int64_t)abslant * s->hw) >> 16);
    if (s->curve < 0) bot -= s->curve;
    if (bot > s->cy + s->hh) bot = s->cy + s->hh;
    if (top >= bot) {
        s->visible = false;
        return;
    }
    if (s->rot || s->bend != 0 || !s->bot_simple) {
        const float k = 1.f / 65536.f;
        s->fc = (float)s->rc * k;
        s->fs = (float)s->rs * k;
        s->fhw = (float)s->hw * k;
        s->fhh = (float)s->hh * k;
        for (int i = 0; i < 4; i++) {
            s->frad[i] = (float)s->rad[i] * k;
            s->frady[i] = (float)s->rady[i] * k;
            s->finv_rx2[i] = s->frad[i] > 1e-3f ? 1.f / (s->frad[i] * s->frad[i]) : 1e6f;
            s->finv_ry2[i] = s->frady[i] > 1e-3f ? 1.f / (s->frady[i] * s->frady[i]) : 1e6f;
        }
        s->ftop = (float)(s->top_base - s->cy) * k;
        s->fbot = (float)(s->bot_base - s->cy) * k;
        s->fslant = (float)s->slant * k;
        s->fbslant = (float)s->bot_slant * k;
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
