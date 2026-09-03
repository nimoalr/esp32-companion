#include "gfx.h"

#include <string.h>
#include "raster.h"

static uint32_t isqrt32(uint32_t v);

uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    const uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

uint16_t gfx_scale(uint16_t color, uint8_t k)
{
    const uint16_t c = (uint16_t)((color >> 8) | (color << 8));
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (r * k + 127) / 255;
    g = (g * k + 127) / 255;
    b = (b * k + 127) / 255;
    const uint16_t o = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((o >> 8) | (o << 8));
}

uint32_t gfx_utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (!p[0]) return 0;
    uint32_t cp;
    int n;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; n = 2; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; n = 3; }
    else { cp = p[0] & 0x07; n = 4; }
    for (int i = 1; i < n; i++) {
        if (!p[i]) { n = i; break; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += n;
    return cp;
}

static const uint8_t *glyph(const gfx_font_t *f, uint32_t cp)
{
    int idx = -1;
    if (cp >= 0x20 && cp < 0x7F) {
        idx = (int)cp - 0x20;
    } else {
        int lo = 0, hi = (int)f->n_extras - 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            if (f->extras[mid] == cp) { idx = 95 + mid; break; }
            if (f->extras[mid] < cp) lo = mid + 1; else hi = mid - 1;
        }
        if (idx < 0) idx = '?' - 0x20;
    }
    return f->bitmap + (size_t)idx * f->h * f->bytes_per_row;
}

void gfx_fill(const gfx_band_t *b, int x, int y, int w, int h, uint16_t color)
{
    int x1 = x + w, y1 = y + h;
    if (x < b->x0) x = b->x0;
    if (y < b->y0) y = b->y0;
    if (x1 > b->x0 + b->w) x1 = b->x0 + b->w;
    if (y1 > b->y0 + b->rows) y1 = b->y0 + b->rows;
    for (int py = y; py < y1; py++) {
        uint16_t *row = b->dst + (size_t)(py - b->y0) * b->w + (x - b->x0);
        for (int px = x; px < x1; px++) *row++ = color;
    }
}

void gfx_text(const gfx_band_t *b, const gfx_font_t *f, int x, int y, const char *utf8, uint16_t fg, int bg)
{
    if (y >= b->y0 + b->rows || y + f->h <= b->y0) return;
    const char *s = utf8;
    uint32_t cp;
    while ((cp = gfx_utf8_next(&s)) != 0) {
        if (x >= b->x0 + b->w) break;
        if (x + f->w > b->x0) {
            const uint8_t *g = glyph(f, cp);
            int r0 = b->y0 - y, r1 = b->y0 + b->rows - y;
            if (r0 < 0) r0 = 0;
            if (r1 > f->h) r1 = f->h;
            for (int r = r0; r < r1; r++) {
                const uint8_t *bits = g + r * f->bytes_per_row;
                uint16_t *row = b->dst + (size_t)(y + r - b->y0) * b->w;
                for (int c = 0; c < f->w; c++) {
                    const int px = x + c;
                    if (px < b->x0 || px >= b->x0 + b->w) continue;
                    const bool on = bits[c >> 3] & (0x80 >> (c & 7));
                    if (on) row[px - b->x0] = fg;
                    else if (bg >= 0) row[px - b->x0] = (uint16_t)bg;
                }
            }
        }
        x += f->w;
    }
}

int gfx_text_width(const gfx_font_t *f, const char *utf8)
{
    int n = 0;
    const char *s = utf8;
    while (gfx_utf8_next(&s)) n++;
    return n * f->w;
}

static const uint16_t *shape_lut(uint16_t color)
{
    static uint16_t lut[256];
    static uint16_t lut_color = 0xFFFF;
    if (lut_color != color) {
        const uint16_t c = (uint16_t)((color >> 8) | (color << 8));
        raster_build_lut(lut, (uint8_t)((c >> 8) & 0xF8), (uint8_t)((c >> 3) & 0xFC), (uint8_t)((c << 3) & 0xF8));
        lut_color = color;
    }
    return lut;
}

void gfx_rrect(const gfx_band_t *b, int x, int y, int w, int h, int r, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (y + h <= b->y0 || y >= b->y0 + b->rows) return;
    const int rmax = (w < h ? w : h) / 2;
    if (r > rmax) r = rmax;
    if (r < 0) r = 0;
    const int32_t hw = (w << 16) / 2, hh = (h << 16) / 2;
    raster_shape_t s = {
        .cx = (x << 16) + hw, .cy = (y << 16) + hh,
        .hw = hw, .hh = hh, .rad = { r << 16, r << 16, r << 16, r << 16 }, .rady = { r << 16, r << 16, r << 16, r << 16 },
        .top_base = (y - 1) << 16, .slant = 0,
        .bot_base = (y + h + 1) << 16, .curve = 0,
        .lut = shape_lut(color),
    };
    raster_shape_finalize(&s, 1 << 14, 1 << 14);
    raster_shapes_over(b->dst, b->x0, b->y0, b->w, b->rows, &s, 1);
}

void gfx_disc(const gfx_band_t *b, int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) return;
    if (cy + r < b->y0 || cy - r > b->y0 + b->rows) return;
    raster_shape_t s = {
        .cx = cx << 16, .cy = cy << 16,
        .hw = r << 16, .hh = r << 16, .rad = { r << 16, r << 16, r << 16, r << 16 }, .rady = { r << 16, r << 16, r << 16, r << 16 },
        .top_base = (cy - r - 1) << 16, .slant = 0,
        .bot_base = (cy + r + 1) << 16, .curve = 0,
        .lut = shape_lut(color),
    };
    raster_shape_finalize(&s, 1 << 14, 1 << 14);
    raster_shapes_over(b->dst, b->x0, b->y0, b->w, b->rows, &s, 1);
}

void gfx_line(const gfx_band_t *b, int x0, int y0, int x1, int y1, int thick, uint16_t color)
{
    /* quick reject on the band */
    const int ylo = (y0 < y1 ? y0 : y1) - thick, yhi = (y0 > y1 ? y0 : y1) + thick;
    if (yhi < b->y0 || ylo >= b->y0 + b->rows) return;
    const int dx = x1 - x0, dy = y1 - y0;
    int len = (int)isqrt32((uint32_t)(dx * dx + dy * dy));
    const int r = thick / 2 > 1 ? thick / 2 : 1;
    const int steps = len / (r > 3 ? r / 2 : 1) + 1;     /* discs overlap by half a radius: a solid stroke */
    for (int i = 0; i <= steps; i++) {
        const int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        gfx_disc(b, x, y, r, color);
    }
}

/* Integer sqrt of a 32-bit value. */
static uint32_t isqrt32(uint32_t v)
{
    uint32_t res = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}

/* Half-width (Q8) of the chord of radius r (px) at vertical offset dy (Q8). */
static int32_t chord_q8(int32_t r_q8, int32_t dy_q8)
{
    const int64_t rr = (int64_t)r_q8 * r_q8 - (int64_t)dy_q8 * dy_q8;
    if (rr <= 0) return -1;
    return (int32_t)isqrt32((uint32_t)rr);
}

/* Sector test: is direction (dx, dy) within [a0, a1] degrees clockwise from 12 o'clock? */
static bool in_sector(int dx, int dy, int a0, int a1)
{
    if (a1 - a0 >= 360) return true;
    /* screen y grows downward: 12 o'clock is (0, -1), clockwise increases x first */
    static const int16_t sin_t[91] = {
        0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49, 53, 58, 62, 66, 71, 75, 79, 83, 88, 92, 96, 100, 104, 108, 112,
        116, 120, 124, 128, 132, 136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187, 190, 193,
        196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228, 230, 232, 234, 236, 237, 239, 241, 242, 243,
        245, 246, 247, 248, 249, 250, 251, 252, 253, 253, 254, 254, 255, 255, 255, 256, 256, 256 };
    #define SIN(a) ((a) <= 90 ? sin_t[(a)] : (a) <= 180 ? sin_t[180 - (a)] : (a) <= 270 ? -sin_t[(a) - 180] : -sin_t[360 - (a)])
    #define COS(a) SIN(((a) + 90) % 360)
    int b0 = ((a0 % 360) + 360) % 360, b1 = ((a1 % 360) + 360) % 360;
    const int sweep = ((a1 - a0) % 360 + 360) % 360;
    /* unit vectors of the two boundaries (screen coords) */
    const int d0x = SIN(b0), d0y = -COS(b0);
    const int d1x = SIN(b1), d1y = -COS(b1);
    /* cross(d0, p) >= 0 means p is clockwise of d0 (screen coords flip the sign) */
    const int64_t c0 = (int64_t)d0x * dy - (int64_t)d0y * dx;   /* >= 0: clockwise of d0 */
    const int64_t c1 = (int64_t)d1x * dy - (int64_t)d1y * dx;   /* <= 0: counter-clockwise of d1 */
    if (sweep <= 180) return c0 >= 0 && c1 <= 0;
    return c0 >= 0 || c1 <= 0;
    #undef SIN
    #undef COS
}

void gfx_ring(const gfx_band_t *b, int cx, int cy, int r_out, int thick, int a0_deg, int a1_deg, uint16_t color)
{
    if (thick <= 0 || r_out <= 0) return;
    const int r_in = r_out - thick;
    int y_a = cy - r_out, y_b = cy + r_out + 1;
    if (y_a < b->y0) y_a = b->y0;
    if (y_b > b->y0 + b->rows) y_b = b->y0 + b->rows;
    const int32_t ro8 = r_out << 8, ri8 = r_in << 8;
    static uint8_t cov[512];

    for (int py = y_a; py < y_b; py++) {
        /* Row extent: outer chord at the sub-row nearest the centre. */
        int32_t dyc = ((py << 8) + 128) - (cy << 8);
        if (dyc < 0) dyc = -dyc;
        int32_t dy_min = dyc - 128; if (dy_min < 0) dy_min = 0;
        const int32_t so_max = chord_q8(ro8, dy_min);
        if (so_max < 0) continue;
        int px0 = cx - ((so_max + 255) >> 8) - 1, px1 = cx + ((so_max + 255) >> 8) + 2;
        if (px0 < b->x0) px0 = b->x0;
        if (px1 > b->x0 + b->w) px1 = b->x0 + b->w;
        if (px0 >= px1) continue;
        const int n = px1 - px0;
        memset(cov, 0, (size_t)n);

        for (int k = 0; k < 4; k++) {
            const int32_t sy = (py << 8) + 32 + 64 * k;             /* sub-row centre, Q8 */
            int32_t dy = sy - (cy << 8); if (dy < 0) dy = -dy;
            const int32_t so = chord_q8(ro8, dy);
            if (so < 0) continue;
            const int32_t si = (r_in > 0) ? chord_q8(ri8, dy) : -1;
            /* spans in Q8 screen x: [cx-so, cx-si] and [cx+si, cx+so] (or one span if no hole) */
            int32_t spans[2][2];
            int ns = 0;
            if (si < 0) { spans[ns][0] = (cx << 8) - so; spans[ns][1] = (cx << 8) + so; ns++; }
            else {
                spans[ns][0] = (cx << 8) - so; spans[ns][1] = (cx << 8) - si; ns++;
                spans[ns][0] = (cx << 8) + si; spans[ns][1] = (cx << 8) + so; ns++;
            }
            for (int s = 0; s < ns; s++) {
                int32_t l = spans[s][0], r = spans[s][1];
                const int32_t lim_l = px0 << 8, lim_r = px1 << 8;
                if (l < lim_l) l = lim_l;
                if (r > lim_r) r = lim_r;
                if (l >= r) continue;
                const int il = l >> 8, ir = (r - 1) >> 8;
                const int w = (k == 3) ? 63 : 64;
                if (il == ir) { cov[il - px0] += (uint8_t)(((r - l) * w) >> 8); continue; }
                cov[il - px0] += (uint8_t)(((((il + 1) << 8) - l) * w) >> 8);
                for (int x = il + 1; x < ir; x++) cov[x - px0] += (uint8_t)w;
                cov[ir - px0] += (uint8_t)(((r - (ir << 8)) * w) >> 8);
            }
        }
        uint16_t *row = b->dst + (size_t)(py - b->y0) * b->w;
        for (int i = 0; i < n; i++) {
            if (!cov[i]) continue;
            const int px = px0 + i;
            if (!in_sector(px - cx, py - cy, a0_deg, a1_deg)) continue;
            row[px - b->x0] = (cov[i] >= 255) ? color : gfx_scale(color, cov[i]);
        }
    }
}
