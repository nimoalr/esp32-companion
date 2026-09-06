/* Expression tints on a few base colours, plus mood extremes. */
#include <stdio.h>
#include <string.h>
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
int main(void) {
    const uint32_t bases[3] = { 0xFF8C00, 0x00E0B0, 0xA060FF };
    const anim_id_t ids[7] = { ANIM_NEUTRAL, ANIM_HAPPY, ANIM_SAD, ANIM_ANGRY, ANIM_LOVE, ANIM_SCARED, ANIM_SLEEPY };
    char p[96];
    for (int b = 0; b < 3; b++) {
        eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
        eyes_init(&eyes, t); eyes_set_base_color(&eyes, bases[b]); anim_init(&sm, &eyes, t);
        for (int i = 0; i < 7; i++) {
            anim_set(&sm, &eyes, ids[i], t); eyes.idle.next_blink_ms = eyes.idle.next_dart_ms = t + 100000000;
            for (uint32_t end = t + 1400; t < end; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
            render(sh); snprintf(p, sizeof p, "out/k_%d_%d.ppm", b, i); ppm_crop(p, 90, 376);
            printf("base %06X %-9s -> %06X\n", bases[b], anim_name(ids[i]), eyes_color(&eyes, 0));
        }
    }
    /* mood extremes on neutral orange */
    for (int m = 0; m < 2; m++) {
        eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
        eyes_init(&eyes, t); anim_init(&sm, &eyes, t); eyes.idle.next_blink_ms = eyes.idle.next_dart_ms = t + 100000000;
        const float e = m ? 1.f : 0.f;
        eyes_set_mood(&eyes, (int32_t)((0.85f + 0.15f * e) * 65536.f), (int32_t)((0.90f + 0.10f * e) * 65536.f));
        for (uint32_t end = t + 600; t < end; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
        render(sh); snprintf(p, sizeof p, "out/m_%d.ppm", m); ppm_crop(p, 90, 376);
        printf("mood energy %.0f -> %06X\n", e, eyes_color(&eyes, 0));
    }
    return 0;
}
