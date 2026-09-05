/* Headphone design alternatives, drawn with the gfx primitives over neutral eyes. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
#include "gfx.h"
uint32_t esp_random(void) { return 0x12345678u; }
#define W 466
#define H 466
#define CX 233
#define CY 233
static uint16_t fb[W * H];
typedef void (*painter_t)(const gfx_band_t *b);
static void render(const raster_shape_t *sh, painter_t paint, const char *label) {
    static uint16_t band[W * 32];
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        raster_band(band, 0, y, W, rows, sh, 2);
        gfx_band_t gb = { band, 0, y, W, rows };
        paint(&gb);
        gfx_text(&gb, &font_spleen_16x32, 60, 400, label, gfx_rgb(200, 200, 210), GFX_TRANSPARENT);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
}
static void ppm(const char *p) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8)); unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 }; fwrite(px, 1, 3, f); }
    fclose(f);
}
static void polar(float deg, float r, int *x, int *y) { const float a = deg * 0.01745329f; *x = CX + (int)lroundf(sinf(a) * r); *y = CY - (int)lroundf(cosf(a) * r); }

/* A: current — thin band, flat discs at ear height */
static void paint_a(const gfx_band_t *b) {
    gfx_ring(b, CX, CY, 208, 11, -78, 78, gfx_rgb(120, 120, 130));
    for (int s = -1; s <= 1; s += 2) { int x = CX + s * 195; gfx_disc(b, x, CY, 30, gfx_rgb(150, 150, 160)); gfx_disc(b, x, CY, 21, gfx_rgb(60, 60, 70)); }
}
/* B: studio over-ear — thick padded band with a highlight, tall oval cups on short arms */
static void paint_b(const gfx_band_t *b) {
    gfx_ring(b, CX, CY, 216, 18, -66, 66, gfx_rgb(70, 70, 80));
    gfx_ring(b, CX, CY, 204, 3, -60, 60, gfx_rgb(140, 140, 150));
    for (int s = -1; s <= 1; s += 2) {
        int x = CX + s * 190;
        gfx_line(b, x, CY - 60, x, CY - 20, 8, gfx_rgb(90, 90, 100));           /* arm */
        gfx_line(b, x, CY - 24, x, CY + 24, 56, gfx_rgb(60, 60, 70));             /* cup body (capsule) */
        gfx_line(b, x, CY - 22, x, CY + 22, 40, gfx_rgb(110, 110, 120));          /* pad */
        gfx_line(b, x, CY - 18, x, CY + 18, 22, gfx_rgb(35, 35, 42));             /* pad centre */
    }
}
/* C: DJ — the whole set sits crooked, one cup lifted off the ear */
static void paint_c(const gfx_band_t *b) {
    const int tilt = -14;
    gfx_ring(b, CX, CY, 208, 12, -86 + tilt, 62 + tilt, gfx_rgb(120, 120, 130));
    int x, y;
    polar(-86.f + tilt, 208.f - 3.f, &x, &y);            /* cup on the ear, at the band's end */
    gfx_disc(b, x, y, 31, gfx_rgb(150, 150, 160)); gfx_disc(b, x, y, 21, gfx_rgb(60, 60, 70));
    polar(62.f + tilt, 208.f - 3.f, &x, &y);             /* cup lifted off the other ear */
    gfx_disc(b, x, y, 31, gfx_rgb(150, 150, 160)); gfx_disc(b, x, y, 21, gfx_rgb(60, 60, 70));
}
/* D: earbuds — no band, a stem beside each ear */
static void paint_d(const gfx_band_t *b) {
    const uint16_t c = gfx_rgb(232, 232, 238);
    for (int s = -1; s <= 1; s += 2) {
        int x = CX + s * 172;
        gfx_disc(b, x, CY - 4, 14, c);
        gfx_line(b, x + s * 2, CY + 4, x + s * 6, CY + 44, 10, c);
        gfx_disc(b, x, CY - 4, 6, gfx_rgb(180, 180, 190));
    }
}
/* E: gamer — dark band with a light strip, glowing rings on the cups */
static void paint_e(const gfx_band_t *b) {
    const uint16_t glow = gfx_rgb(0, 220, 255);
    gfx_ring(b, CX, CY, 212, 15, -72, 72, gfx_rgb(55, 55, 65));
    gfx_ring(b, CX, CY, 202, 3, -68, 68, glow);
    for (int s = -1; s <= 1; s += 2) {
        int x = CX + s * 192;
        gfx_disc(b, x, CY, 34, gfx_rgb(55, 55, 65));
        gfx_ring(b, x, CY, 24, 4, 0, 360, glow);
        gfx_disc(b, x, CY, 6, glow);
    }
}
/* F: cat ears — band with two pink-lined ears */
static void paint_f(const gfx_band_t *b) {
    const uint16_t grey = gfx_rgb(200, 200, 210), pink = gfx_rgb(255, 120, 170);
    gfx_ring(b, CX, CY, 208, 12, -78, 78, grey);
    for (int s = -1; s <= 1; s += 2) {
        int x = CX + s * 195;
        gfx_disc(b, x, CY, 30, grey); gfx_disc(b, x, CY, 21, pink);
        int ax, ay, bx, by, tx, ty;
        polar(s * 30.f, 204.f, &ax, &ay); polar(s * 54.f, 204.f, &bx, &by); polar(s * 42.f, 268.f, &tx, &ty);
        /* fill: fan of lines from the tip to the base, then the outline */
        for (int i = 0; i <= 14; i++) { int px = ax + (bx - ax) * i / 14, py = ay + (by - ay) * i / 14; gfx_line(b, tx, ty, px, py, 9, pink); }
        gfx_line(b, ax, ay, tx, ty, 8, grey); gfx_line(b, bx, by, tx, ty, 8, grey);
    }
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t); eyes.idle.next_blink_ms = eyes.idle.next_dart_ms = t + 100000000;
    anim_set(&sm, &eyes, ANIM_HAPPY, t);
    for (uint32_t end = t + 1200; t < end; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
    painter_t p[6] = { paint_a, paint_b, paint_c, paint_d, paint_e, paint_f };
    const char *names[6] = { "A  current", "B  studio", "C  dj tilt", "D  earbuds", "E  gamer", "F  cat ears" };
    for (int i = 0; i < 6; i++) { render(sh, p[i], names[i]); char f[64]; snprintf(f, sizeof f, "out/hp_%d.ppm", i); ppm(f); }
    return 0;
}
