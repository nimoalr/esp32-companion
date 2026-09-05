/* Render every expression (settled, mid-motion) and a synthetic dance sequence. */
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
static void ppm(const char *p) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8)); unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 }; fwrite(px, 1, 3, f); }
    fclose(f);
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t);
    eyes.idle.next_blink_ms = t + 10000000; eyes.idle.next_dart_ms = t + 10000000;
    for (int a = ANIM_CURIOUS; a < ANIM_DANCE; a++) {
        anim_set(&sm, &eyes, (anim_id_t)a, t);
        for (uint32_t end = t + 1300; t < end; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
        render(sh); char p[64]; snprintf(p, sizeof p, "out/x_%02d_%s.ppm", a, anim_name((anim_id_t)a)); ppm(p);
        printf("%-10s L[%d,%d)x[%d,%d) R[%d,%d)x[%d,%d)\n", anim_name((anim_id_t)a), sh[0].px0, sh[0].px1, sh[0].py0, sh[0].py1, sh[1].px0, sh[1].px1, sh[1].py0, sh[1].py1);
        t += 200;
    }
    /* dance: synthetic 120 bpm track */
    anim_set(&sm, &eyes, ANIM_DANCE, t);
    audio_features_t af = { .active = true, .loud = 0.7f, .bass = 0.2f, .balance = 0.3f };
    uint32_t next_beat = t + 100; int shots = 0; const uint32_t shot_at[] = { 30, 120, 260, 420 };
    for (uint32_t end = t + 3000; t < end; t += 16) {
        if (t >= next_beat) { af.beat_count++; af.last_beat_ms = t; af.bass = 1.0f; next_beat += 500; } else af.bass *= 0.85f;
        anim_set_audio(&sm, &af); anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh);
        if (shots < 4 && af.last_beat_ms && t - af.last_beat_ms >= shot_at[shots] && t - af.last_beat_ms < shot_at[shots] + 16 && af.beat_count == 3) {
            render(sh); char p[64]; snprintf(p, sizeof p, "out/dance_%d.ppm", shots); ppm(p); shots++;
        }
    }
    printf("dance shots %d\n", shots);
    return 0;
}
