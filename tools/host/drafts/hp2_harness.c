/* Flat-style headphones after the reference image: two-tone band, tall cups with a darker back. */
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
typedef struct { uint16_t band_dark, band_mid, cup_back, cup_front, cup_edge, ear_fill; } pal_t;
static void ppm(const char *p) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8)); unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 }; fwrite(px, 1, 3, f); }
    fclose(f);
}
static void polar(float deg, float r, int cy, int *x, int *y) { const float a = deg * 0.01745329f; *x = CX + (int)lroundf(sinf(a) * r); *y = cy - (int)lroundf(cosf(a) * r); }

#define BAND_CY   240
#define BAND_R    200
#define BAND_T    40
#define BAND_A    78
#define CUP_CY    262
#define CUP_DX    195     /* cup centre offset from the screen centre */
static void paint_flat(const gfx_band_t *b, const pal_t *p, bool ears) {
    /* band: two tones, rounded ends */
    gfx_ring(b, CX, BAND_CY, BAND_R, BAND_T, -BAND_A, BAND_A, p->band_mid);
    gfx_ring(b, CX, BAND_CY, BAND_R, 15, -BAND_A, BAND_A, p->band_dark);
    for (int s = -1; s <= 1; s += 2) {
        int ex, ey; polar(s * (float)BAND_A, BAND_R - BAND_T / 2.f, BAND_CY, &ex, &ey);
        gfx_disc(b, ex, ey, BAND_T / 2, p->band_mid);
        gfx_disc(b, ex - s * 3, ey - 6, 9, p->band_dark);
        /* stem into the cup */
        gfx_line(b, ex, ey, CX + s * CUP_DX, CUP_CY - 50, 9, p->band_dark);
        /* cup: darker back peeking out on the outside, front face, edge highlight on the inside */
        const int cx = CX + s * CUP_DX;
        gfx_rrect(b, cx - 30 + s * 12, CUP_CY - 62, 60, 124, 24, p->cup_back);
        gfx_rrect(b, cx - 28, CUP_CY - 58, 56, 116, 20, p->cup_front);
        gfx_rrect(b, cx - 28 - s * 22, CUP_CY - 50, 12, 100, 6, p->cup_edge);
        if (ears) {
            int ax, ay, bx, by, tx, ty;
            polar(s * 28.f, BAND_R - 8.f, BAND_CY, &ax, &ay); polar(s * 56.f, BAND_R - 8.f, BAND_CY, &bx, &by); polar(s * 42.f, BAND_R + 62.f, BAND_CY, &tx, &ty);
            for (int i = 0; i <= 16; i++) { int px = ax + (bx - ax) * i / 16, py = ay + (by - ay) * i / 16; gfx_line(b, tx, ty, px, py, 10, p->band_mid); }
            int ix, iy, jx, jy, kx, ky;
            polar(s * 34.f, BAND_R - 2.f, BAND_CY, &ix, &iy); polar(s * 50.f, BAND_R - 2.f, BAND_CY, &jx, &jy); polar(s * 42.f, BAND_R + 40.f, BAND_CY, &kx, &ky);
            for (int i = 0; i <= 10; i++) { int px = ix + (jx - ix) * i / 10, py = iy + (jy - iy) * i / 10; gfx_line(b, kx, ky, px, py, 7, p->ear_fill); }
        }
    }
}
static void render(const raster_shape_t *sh, const pal_t *p, bool ears, const char *label) {
    static uint16_t band[W * 32];
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        raster_band(band, 0, y, W, rows, sh, 2);
        gfx_band_t gb = { band, 0, y, W, rows };
        paint_flat(&gb, p, ears);
        gfx_text(&gb, &font_spleen_16x32, 60, 400, label, gfx_rgb(200, 200, 210), GFX_TRANSPARENT);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t); eyes.idle.next_blink_ms = eyes.idle.next_dart_ms = t + 100000000;
    anim_set(&sm, &eyes, ANIM_HAPPY, t);
    for (uint32_t end = t + 1200; t < end; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
    const pal_t blue = { gfx_rgb(32, 42, 100), gfx_rgb(52, 66, 150), gfx_rgb(34, 44, 104), gfx_rgb(62, 82, 172), gfx_rgb(90, 112, 205), gfx_rgb(240, 110, 160) };
    const pal_t graphite = { gfx_rgb(38, 38, 46), gfx_rgb(88, 88, 100), gfx_rgb(44, 44, 52), gfx_rgb(112, 112, 126), gfx_rgb(160, 160, 176), gfx_rgb(240, 110, 160) };
    const pal_t cream = { gfx_rgb(150, 120, 90), gfx_rgb(232, 214, 186), gfx_rgb(160, 130, 100), gfx_rgb(245, 232, 210), gfx_rgb(255, 250, 240), gfx_rgb(240, 110, 160) };
    render(sh, &blue, false, "G  flat blue"); ppm("out/hq_0.ppm");
    render(sh, &graphite, false, "H  flat graphite"); ppm("out/hq_1.ppm");
    render(sh, &cream, false, "I  flat cream"); ppm("out/hq_2.ppm");
    render(sh, &blue, true, "J  blue + ears"); ppm("out/hq_3.ppm");
    render(sh, &graphite, true, "K  graphite + ears"); ppm("out/hq_4.ppm");
    render(sh, &cream, true, "L  cream + ears"); ppm("out/hq_5.ppm");
    return 0;
}
