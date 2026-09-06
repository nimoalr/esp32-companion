/* Expression sheet plus blink / dart / transition strips. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
#include "board.h"
uint32_t esp_random(void) { return 0x12345678u; }
#define W 466
#define H 466
static uint16_t fb[W * H], band[W * 32];
static void render(const raster_shape_t *sh) {
    for (int y = 0; y < H; y += 32) { int rows = H - y < 32 ? H - y : 32; raster_band(band, 0, y, W, rows, sh, 2); memcpy(&fb[y * W], band, (size_t)W * rows * 2); }
}
static void ppm_crop(const char *p, int y0, int y1) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, y1 - y0);
    for (int i = y0 * W; i < y1 * W; i++) { uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8)); unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 }; fwrite(px, 1, 3, f); }
    fclose(f);
}
static void still(eyes_t *e, uint32_t t) { e->idle.next_blink_ms = t + 100000000; e->idle.next_dart_ms = t + 100000000; }
static void run(anim_sm_t *sm, eyes_t *e, raster_shape_t *sh, uint32_t *t, int ms) {
    for (uint32_t end = *t + ms; *t < end; *t += 16) { anim_update(sm, e, *t); eyes_update(e, *t, sh); }
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t); still(&eyes, t);
    char p[96];
    /* 1. every expression, settled */
    for (int a = 0; a < ANIM_DANCE; a++) {
        anim_set(&sm, &eyes, (anim_id_t)a, t); still(&eyes, t);
        run(&sm, &eyes, sh, &t, 1400);
        render(sh); snprintf(p, sizeof p, "out/e_%02d.ppm", a); ppm_crop(p, 60, 406);
    }
    /* 2. blink strip: neutral, force a blink, 10 frames 34 ms apart */
    anim_set(&sm, &eyes, ANIM_NEUTRAL, t); still(&eyes, t); run(&sm, &eyes, sh, &t, 600);
    eyes_blink_now(&eyes, t);
    for (int i = 0; i < 11; i++) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); render(sh); snprintf(p, sizeof p, "out/b_%02d.ppm", i); ppm_crop(p, 133, 333); t += 34; }
    /* 3. blink strip on an asymmetric pose (curious): the eyes line up as they close */
    anim_set(&sm, &eyes, ANIM_CURIOUS, t); still(&eyes, t); run(&sm, &eyes, sh, &t, 1400);
    eyes_blink_now(&eyes, t);
    for (int i = 0; i < 11; i++) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); render(sh); snprintf(p, sizeof p, "out/c_%02d.ppm", i); ppm_crop(p, 133, 333); t += 34; }
    /* 4. long dart strip: force a dart from (-14, 6) to (14, -8) */
    anim_set(&sm, &eyes, ANIM_NEUTRAL, t); still(&eyes, t); run(&sm, &eyes, sh, &t, 600);
    eyes.idle.dart_to_x = -14 << 16; eyes.idle.dart_to_y = 6 << 16; eyes.idle.dart_from_x = eyes.idle.dart_to_x; eyes.idle.dart_from_y = eyes.idle.dart_to_y;
    eyes.idle.dart_t0_ms = t - 1000; eyes.idle.dart_dur_ms = 1; eyes.idle.dart_squash = 0;
    run(&sm, &eyes, sh, &t, 100);
    eyes.idle.rng = 7; eyes.idle.next_dart_ms = t;   /* fires now with a random target; override it right after */
    anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh);
    eyes.idle.dart_to_x = 14 << 16; eyes.idle.dart_to_y = -8 << 16; eyes.idle.dart_dur_ms = 130; eyes.idle.dart_squash = Q16(0.28); eyes.idle.dart_lag_x = false; eyes.idle.blinking = false;
    for (int i = 0; i < 10; i++) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); render(sh); snprintf(p, sizeof p, "out/d_%02d.ppm", i); ppm_crop(p, 133, 333); t += 16; }
    /* 5. transition strip: neutral -> surprised (anticipation + overshoot), frames 30 ms apart */
    anim_set(&sm, &eyes, ANIM_NEUTRAL, t); still(&eyes, t); run(&sm, &eyes, sh, &t, 600);
    anim_set(&sm, &eyes, ANIM_SURPRISED, t); still(&eyes, t);
    for (int i = 0; i < 10; i++) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); render(sh); snprintf(p, sizeof p, "out/s_%02d.ppm", i); ppm_crop(p, 40, 426); t += 30; }
    /* 6. look-around move: stretch along the motion */
    anim_set(&sm, &eyes, ANIM_LOOK_AROUND, t); still(&eyes, t);
    run(&sm, &eyes, sh, &t, 900);   /* at t=900 the second keyframe fires: -16 -> +16 in 300 ms */
    for (int i = 0; i < 10; i++) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); render(sh); snprintf(p, sizeof p, "out/l_%02d.ppm", i); ppm_crop(p, 133, 333); t += 32; }
    /* 7. gaze scaling: looking up / centre / down / left */
    static const int look[4][2] = { { 0, -22 }, { 0, 0 }, { 0, 22 }, { -22, 0 } };
    for (int i = 0; i < 4; i++) {
        anim_set(&sm, &eyes, ANIM_NEUTRAL, t); still(&eyes, t);
        eyes.idle.dart_to_x = eyes.idle.dart_from_x = look[i][0] << 16; eyes.idle.dart_to_y = eyes.idle.dart_from_y = look[i][1] << 16; eyes.idle.dart_dur_ms = 1; eyes.idle.dart_t0_ms = t - 10;
        run(&sm, &eyes, sh, &t, 600); render(sh); snprintf(p, sizeof p, "out/g_%d.ppm", i); ppm_crop(p, 60, 406);
        printf("gaze %d: eye w %.1f h %.1f\n", i, (sh[0].hw * 2) / 65536.f, (sh[0].hh * 2) / 65536.f);
    }
    return 0;
}
