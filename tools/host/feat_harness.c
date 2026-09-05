/* Hot spot on/off, elliptical corners, lower lids, attention, face scale. */
#include <stdio.h>
#include <string.h>
#include <time.h>
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
static double bench(const raster_shape_t *sh) {
    clock_t c0 = clock();
    for (int it = 0; it < 1000; it++) for (int e = 0; e < 2; e++) {
        int w = sh[e].px1 - sh[e].px0;
        for (int y = sh[e].py0; y < sh[e].py1; y += 32) { int rows = sh[e].py1 - y < 32 ? sh[e].py1 - y : 32; raster_band(band, sh[e].px0, y, w, rows, sh, 2); }
    }
    return (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / 1000;
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t); still(&eyes, t);
    char p[96];
    const anim_id_t ids[6] = { ANIM_NEUTRAL, ANIM_HAPPY, ANIM_SAD, ANIM_ANGRY, ANIM_LOVE, ANIM_SLEEPY };
    /* row 1: hot spot off; row 2: hot spot on */
    for (int hot = 0; hot < 2; hot++) {
        eyes_set_hotspot(&eyes, hot);
        for (int i = 0; i < 6; i++) {
            anim_set(&sm, &eyes, ids[i], t); still(&eyes, t); run(&sm, &eyes, sh, &t, 1400);
            render(sh); snprintf(p, sizeof p, "out/f_%d_%d.ppm", hot, i); ppm_crop(p, 90, 376);
            printf("%-8s hot=%d  %6.1f us/frame host  rad_eq=%d bot_simple=%d\n", anim_name(ids[i]), hot, bench(sh), sh[0].rad_equal, sh[0].bot_simple);
        }
    }
    /* hot spot follows the gaze: looking left / right, and rotated 30 deg */
    eyes_set_hotspot(&eyes, true);
    anim_set(&sm, &eyes, ANIM_NEUTRAL, t); still(&eyes, t);
    static const int look[3][2] = { { -20, 6 }, { 20, -8 }, { 0, 0 } };
    for (int i = 0; i < 3; i++) {
        eyes.idle.dart_to_x = eyes.idle.dart_from_x = look[i][0] << 16; eyes.idle.dart_to_y = eyes.idle.dart_from_y = look[i][1] << 16; eyes.idle.dart_dur_ms = 1; eyes.idle.dart_t0_ms = t - 10;
        eyes_set_face_angle(&eyes, i == 2 ? 30.f : 0.f);
        run(&sm, &eyes, sh, &t, 400); render(sh); snprintf(p, sizeof p, "out/h_%d.ppm", i); ppm_crop(p, 60, 406);
    }
    eyes_set_face_angle(&eyes, 0.f); eyes_set_hotspot(&eyes, false);
    /* attention: finger at top-left, then bottom-right; frames 100 ms apart */
    anim_set(&sm, &eyes, ANIM_NEUTRAL, t); still(&eyes, t); run(&sm, &eyes, sh, &t, 400);
    eyes_set_attention(&eyes, true, 80, 90);
    for (int i = 0; i < 4; i++) { run(&sm, &eyes, sh, &t, 100); render(sh); snprintf(p, sizeof p, "out/a_%d.ppm", i); ppm_crop(p, 60, 406); }
    eyes_set_attention(&eyes, true, 400, 380);
    for (int i = 4; i < 8; i++) { run(&sm, &eyes, sh, &t, 100); render(sh); snprintf(p, sizeof p, "out/a_%d.ppm", i); ppm_crop(p, 60, 406); }
    printf("attend: dart scale in effect %.2f, gaze %.1f,%.1f\n", eyes.idle.dart_scale / 65536.f, eyes.attend_x / 65536.f, eyes.attend_y / 65536.f);
    eyes_set_attention(&eyes, false, 0, 0);
    /* face scale 0.8 / 1.0 / 1.25 and offset */
    static const float sc[3] = { 0.8f, 1.0f, 1.25f };
    for (int i = 0; i < 3; i++) {
        eyes_set_face_scale(&eyes, (int32_t)(sc[i] * 65536.f)); eyes_set_face_offset(&eyes, (i - 1) * 30 << 16, 0);
        run(&sm, &eyes, sh, &t, 400); render(sh); snprintf(p, sizeof p, "out/s_%d.ppm", i); ppm_crop(p, 60, 406);
    }
    return 0;
}
