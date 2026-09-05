#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
#include "board.h"

uint32_t esp_random(void) { return 0x12345678u; }

#define W BOARD_LCD_H_RES
#define H BOARD_LCD_V_RES
static uint16_t fb[W * H];
static uint16_t band[W * 32];

static void render_full(const raster_shape_t *shapes) {
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        raster_band(band, 0, y, W, rows, shapes, 2);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
}

static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t s = fb[i];
        uint16_t c = (uint16_t)((s >> 8) | (s << 8));   /* undo panel byte order */
        unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2];
    uint32_t t = 1000;
    eyes_init(&eyes, t);
    anim_init(&sm, &eyes, t);
    /* disable idle randomness for deterministic stills: push blink/saccade far away */
    eyes.idle.next_blink_ms = t + 1000000; eyes.idle.next_dart_ms = t + 1000000;

    const char *names[] = {"neutral","happy","sad","angry","surprised","sleepy","look_around","wink"};
    const uint32_t settle[] = {400, 400, 1400, 400, 400, 3200, 1200, 600};
    for (int a = 0; a < ANIM_COUNT; a++) {
        if (a) anim_next(&sm, &eyes, t);
        uint32_t t_end = t + settle[a];
        /* step time in 16 ms increments so keyframes fire in order */
        for (; t < t_end; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
        anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh);
        render_full(sh);
        char p[128]; snprintf(p, sizeof p, "out/%d_%s.ppm", a, names[a]);
        write_ppm(p);
        printf("%s: L bbox [%d,%d)x[%d,%d) vis=%d  R bbox [%d,%d)x[%d,%d) vis=%d\n", names[a],
               sh[0].px0, sh[0].px1, sh[0].py0, sh[0].py1, sh[0].visible,
               sh[1].px0, sh[1].px1, sh[1].py0, sh[1].py1, sh[1].visible);
        t += 100;
    }
    /* mid-blink still on neutral */
    anim_next(&sm, &eyes, t); /* wraps to neutral */
    for (uint32_t e = t; e < t + 400; e += 16) { anim_update(&sm, &eyes, e); eyes_update(&eyes, e, sh); }
    t += 400;
    eyes.idle.next_blink_ms = t;  /* blink starts now */
    for (int i = 0; i < 4; i++) {
        uint32_t tt = t + 15 + i * 30;
        anim_update(&sm, &eyes, tt); eyes_update(&eyes, tt, sh);
        render_full(sh);
        char p[128]; snprintf(p, sizeof p, "out/blink_%d.ppm", i);
        write_ppm(p);
        printf("blink %d: L bbox [%d,%d)x[%d,%d) vis=%d bytes=%d\n", i, sh[0].px0, sh[0].px1, sh[0].py0, sh[0].py1, sh[0].visible,
               (sh[0].px1-sh[0].px0)*(sh[0].py1-sh[0].py0)*2);
    }
    /* timing: rasterise neutral eyes 1000 times over the two eye bboxes */
    eyes.idle.next_blink_ms = t + 1000000;
    anim_update(&sm, &eyes, t + 2000); eyes_update(&eyes, t + 2000, sh);
    clock_t c0 = clock();
    long px = 0;
    for (int it = 0; it < 1000; it++) {
        for (int e = 0; e < 2; e++) {
            int w = sh[e].px1 - sh[e].px0;
            for (int y = sh[e].py0; y < sh[e].py1; y += 32) {
                int rows = sh[e].py1 - y < 32 ? sh[e].py1 - y : 32;
                raster_band(band, sh[e].px0, y, w, rows, sh, 2);
                px += w * rows;
            }
        }
    }
    double s = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("host raster: %.1f us/frame (both eyes), %ld px/frame\n", s * 1e6 / 1000, px / 1000);
    return 0;
}
