#include "raster.h"

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

/*
 * Visible x-intervals of shape `s` on the sub-scanline at y (Q16).
 * Returns 0, 1 or 2 spans.
 */
static inline int IRAM_ATTR shape_spans(const raster_shape_t *s, int32_t y, span_t out[2])
{
    int32_t dy = y - s->cy;
    if (dy < 0) {
        dy = -dy;
    }
    if (dy >= s->hh) {
        return 0;
    }

    int32_t half;
    const int32_t inner = s->hh - s->r;
    if (dy <= inner) {
        half = s->hw;
    } else {
        const int32_t d = dy - inner;
        const int64_t rr = (int64_t)s->r * s->r - (int64_t)d * d;   /* Q32 */
        if (rr <= 0) {
            return 0;
        }
        half = (s->hw - s->r) + sqrt_q16((uint32_t)(rr >> 16));
    }
    int32_t xl = s->cx - half;
    int32_t xr = s->cx + half;

    /* Top lid: visible where y >= top_base + slant * (x - cx). */
    const int32_t dt = y - s->top_base;
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
    if (xl >= xr) {
        return 0;
    }

    /* Bottom lid: visible where y <= bot_base - curve * (1 - t^2), t = (x - cx) / hw. */
    const int32_t db = s->bot_base - y;
    if (db < 0) {
        return 0;
    }
    if (s->curve > 0 && db < s->curve) {
        /* t^2 >= 1 - db / curve  ->  |x - cx| >= hw * sqrt(q) */
        int32_t q = Q16_ONE - (int32_t)(((int64_t)db * s->curve_rcp) >> 16);
        if (q < 0) {
            q = 0;
        }
        const int32_t g = (int32_t)(((int64_t)s->hw * sqrt_q16((uint32_t)q)) >> 16);
        const int32_t gl = s->cx - g;
        const int32_t gr = s->cx + g;
        int n = 0;
        if (xl < gl) {
            out[n].l = xl;
            out[n].r = gl < xr ? gl : xr;
            n++;
        }
        if (gr < xr) {
            out[n].l = gr > xl ? gr : xl;
            out[n].r = xr;
            n++;
        }
        return n;
    }

    out[0].l = xl;
    out[0].r = xr;
    return 1;
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

static inline void IRAM_ATTR blit_cov(uint16_t *dst, const uint8_t *cov, int n, const uint16_t *lut)
{
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
    span_t sp[4][2];
    int ns[4];
    int total = 0;
    bool multi = false;
    const int32_t ybase = py << 16;

    for (int k = 0; k < 4; k++) {
        ns[k] = shape_spans(s, ybase + k_sub_off[k], sp[k]);
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

void IRAM_ATTR raster_band(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes)
{
    memset(dst, 0, (size_t)w * (size_t)rows * 2u);
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

void raster_shape_finalize(raster_shape_t *s, int screen_w, int screen_h)
{
    if (s->slant > -RCP_MIN_Q16 && s->slant < RCP_MIN_Q16) s->slant = 0;
    if (s->curve < RCP_MIN_Q16) s->curve = 0;
    s->slant_rcp = s->slant ? (int32_t)((1LL << 32) / s->slant) : 0;
    s->curve_rcp = s->curve ? (int32_t)((1LL << 32) / s->curve) : 0;

    if (s->hw <= 0 || s->hh <= 0) {
        s->visible = false;
        return;
    }
    /* Vertical extent after the lids: the top line is lowest at |x - cx| = hw. */
    int32_t aslant = s->slant < 0 ? -s->slant : s->slant;
    int32_t top = s->top_base - (int32_t)(((int64_t)aslant * s->hw) >> 16);
    if (top < s->cy - s->hh) top = s->cy - s->hh;
    int32_t bot = s->bot_base;
    if (bot > s->cy + s->hh) bot = s->cy + s->hh;
    if (top >= bot) {
        s->visible = false;
        return;
    }
    int x0 = (s->cx - s->hw) >> 16;
    int x1 = ((s->cx + s->hw + 0xFFFF) >> 16) + 1;
    int y0 = top >> 16;
    int y1 = ((bot + 0xFFFF) >> 16) + 1;
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
