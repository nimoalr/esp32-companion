/* A still of the spectrum eyes: the dance face with sample bar heights, for docs/images/spectrum_eyes.png. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
#include "board.h"

uint32_t esp_random(void) { return 0x12345678u; }
#define W BOARD_LCD_H_RES
#define H BOARD_LCD_V_RES
static uint16_t fb[W * H];
static uint16_t band[W * 32];

int main(void)
{
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2];
    uint32_t t = 1000;
    eyes_init(&eyes, t);
    anim_init(&sm, &eyes, t);
    eyes.idle.next_blink_ms = t + 1000000; eyes.idle.next_dart_ms = t + 1000000;
    anim_set(&sm, &eyes, ANIM_HAPPY, t);
    for (int k = 0; k < 40; k++) { t += 16; anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
    const float hl[8] = { 0.9f, 0.75f, 0.6f, 0.8f, 0.45f, 0.35f, 0.5f, 0.3f };
    const float hr[8] = { 0.4f, 0.55f, 0.3f, 0.25f, 0.35f, 0.2f, 0.15f, 0.1f };
    eyes_set_bars(&eyes, true);
    eyes_set_bar_heights(&eyes, 0, hl);
    eyes_set_bar_heights(&eyes, 1, hr);
    t += 16; anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh);
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        raster_band(band, 0, y, W, rows, sh, 2);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
    FILE *f = fopen("out/spectrum.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t s = fb[i];
        uint16_t c = (uint16_t)((s >> 8) | (s << 8));
        unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return 0;
}
