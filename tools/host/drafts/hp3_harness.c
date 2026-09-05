/* Third round of headphone drafts: chunky cartoon silhouettes that read on black. */
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
static void ppm(const char *p) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8)); unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 }; fwrite(px, 1, 3, f); }
    fclose(f);
}
static void polar(float deg, float r, int cy, int *x, int *y) { const float a = deg * 0.01745329f; *x = CX + (int)lroundf(sinf(a) * r); *y = cy - (int)lroundf(cosf(a) * r); }
typedef struct { uint16_t body, shade, light, accent; } pal_t;

/* M: big round cups hugging the sides, very thick band, concentric pad */
static void paint_m(const gfx_band_t *b, const pal_t *p) {
    gfx_ring(b, CX, 248, 222, 46, -84, 84, p->body);
    gfx_ring(b, CX, 248, 222, 46, -84, -30, p->light);      /* light catch top-left */
    gfx_ring(b, CX, 248, 222, 14, -84, 84, p->shade);       /* dark outer rim */
    gfx_ring(b, CX, 248, 222, 46, -84, -30, p->light);
    gfx_ring(b, CX, 248, 222, 14, -84, -30, p->body);
    for (int s = -1; s <= 1; s += 2) {
        const int cx = CX + s * 212, cy = 262;
        gfx_line(b, cx - s * 10, cy - 78, cx - s * 4, cy - 40, 14, p->shade);   /* yoke */
        gfx_disc(b, cx + s * 6, cy, 66, p->shade);                             /* back */
        gfx_disc(b, cx, cy, 60, p->body);
        gfx_disc(b, cx - s * 8, cy, 40, p->shade);                             /* pad */
        gfx_disc(b, cx - s * 8, cy, 26, p->accent);
    }
}
/* N: square-ish cups (rounded rects) with a bold yoke, band lighter inside */
static void paint_n(const gfx_band_t *b, const pal_t *p) {
    gfx_ring(b, CX, 244, 218, 44, -80, 80, p->shade);
    gfx_ring(b, CX, 244, 204, 26, -78, 78, p->body);
    for (int s = -1; s <= 1; s += 2) {
        int ex, ey; polar(s * 80.f, 196.f, 244, &ex, &ey);
        gfx_disc(b, ex, ey, 22, p->shade);
        gfx_line(b, ex, ey, ex + s * 6, ey + 46, 14, p->shade);
        const int cx = CX + s * 205, cy = 272;
        gfx_rrect(b, cx - 44 + s * 8, cy - 78, 88, 156, 34, p->shade);
        gfx_rrect(b, cx - 40, cy - 72, 80, 144, 30, p->body);
        gfx_rrect(b, cx - 40 - s * 24, cy - 60, 16, 120, 8, p->light);
        gfx_disc(b, cx - s * 6, cy, 12, p->accent);
    }
}
/* O: band behind the head (low, at the back) with big cups: only the cup tops and a thin bridge show */
static void paint_o(const gfx_band_t *b, const pal_t *p) {
    gfx_ring(b, CX, 262, 226, 16, -70, 70, p->shade);      /* thin bridge over the top */
    for (int s = -1; s <= 1; s += 2) {
        const int cx = CX + s * 214, cy = 262;
        gfx_line(b, cx - s * 14, cy - 96, cx - s * 4, cy - 56, 12, p->shade);
        gfx_rrect(b, cx - 48 + s * 6, cy - 84, 96, 168, 44, p->shade);
        gfx_rrect(b, cx - 44, cy - 78, 88, 156, 40, p->body);
        gfx_rrect(b, cx - 44 - s * 26, cy - 62, 14, 124, 7, p->light);
        gfx_ring(b, cx - s * 4, cy, 24, 6, 0, 360, p->accent);
    }
}
/* P: pill cups, visible slider rails, band with a centre pad */
static void paint_p(const gfx_band_t *b, const pal_t *p) {
    gfx_ring(b, CX, 246, 220, 40, -76, 76, p->body);
    gfx_ring(b, CX, 246, 220, 40, -22, 22, p->shade);       /* centre pad */
    gfx_ring(b, CX, 246, 208, 4, -74, 74, p->light);
    for (int s = -1; s <= 1; s += 2) {
        int ex, ey; polar(s * 76.f, 200.f, 246, &ex, &ey);
        gfx_disc(b, ex, ey, 20, p->body);
        gfx_line(b, ex, ey + 6, ex + s * 4, ey + 58, 10, p->light);   /* slider rail */
        gfx_line(b, ex, ey + 6, ex + s * 4, ey + 58, 4, p->shade);
        const int cx = CX + s * 208, cy = 280;
        gfx_line(b, cx + s * 6, cy - 56, cx + s * 6, cy + 56, 88, p->shade);
        gfx_line(b, cx, cy - 52, cx, cy + 52, 80, p->body);
        gfx_line(b, cx - s * 22, cy - 40, cx - s * 22, cy + 40, 14, p->light);
        gfx_disc(b, cx - s * 2, cy, 10, p->accent);
    }
}
static void render(const raster_shape_t *sh, void (*paint)(const gfx_band_t *, const pal_t *), const pal_t *p, const char *label) {
    static uint16_t band[W * 32];
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        raster_band(band, 0, y, W, rows, sh, 2);
        gfx_band_t gb = { band, 0, y, W, rows };
        paint(&gb, p);
        gfx_text(&gb, &font_spleen_16x32, 40, 410, label, gfx_rgb(200, 200, 210), GFX_TRANSPARENT);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t); eyes.idle.next_blink_ms = eyes.idle.next_dart_ms = t + 100000000;
    anim_set(&sm, &eyes, ANIM_HAPPY, t);
    for (uint32_t end = t + 1200; t < end; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
    const pal_t white = { gfx_rgb(214, 214, 222), gfx_rgb(96, 96, 110), gfx_rgb(250, 250, 255), gfx_rgb(255, 140, 0) };
    const pal_t slate = { gfx_rgb(110, 118, 140), gfx_rgb(48, 52, 66), gfx_rgb(170, 178, 200), gfx_rgb(255, 140, 0) };
    render(sh, paint_m, &white, "M  round cups"); ppm("out/hr_0.ppm");
    render(sh, paint_n, &white, "N  square cups"); ppm("out/hr_1.ppm");
    render(sh, paint_o, &white, "O  tall cups, low band"); ppm("out/hr_2.ppm");
    render(sh, paint_p, &white, "P  pill cups, rails"); ppm("out/hr_3.ppm");
    render(sh, paint_m, &slate, "M  slate"); ppm("out/hr_4.ppm");
    render(sh, paint_n, &slate, "N  slate"); ppm("out/hr_5.ppm");
    return 0;
}
