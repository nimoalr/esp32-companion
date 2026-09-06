/* Two stills: the eyes leaning toward a voice at either end of the mic axis (the behaviour's
 * VOICE_LEAN_PX), and the neutral pose between them, for docs/images/voice_lean.png. */
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

static void render_full(const raster_shape_t *shapes)
{
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        raster_band(band, 0, y, W, rows, shapes, 2);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t s = fb[i];
        uint16_t c = (uint16_t)((s >> 8) | (s << 8));
        unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(void)
{
    static const float lean[3] = { 55.f, 0.f, -55.f };   /* dy: + is down; a voice at the USB end (bottom) pulls the eyes down */
    static const char *names[3] = { "usb_end", "centre", "lanyard_end" };
    for (int i = 0; i < 3; i++) {
        eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2];
        uint32_t t = 1000;
        eyes_init(&eyes, t);
        anim_init(&sm, &eyes, t);
        eyes.idle.next_blink_ms = t + 1000000; eyes.idle.next_dart_ms = t + 1000000;
        anim_set(&sm, &eyes, ANIM_CURIOUS, t);
        eye_pose_t env = { 0 };
        env.dy = (int32_t)(lean[i] * 65536.f);
        eyes_set_env(&eyes, 0, &env);
        eyes_set_env(&eyes, 1, &env);
        for (uint32_t k = 0; k < 60; k++) { t += 16; anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
        render_full(sh);
        char p[128]; snprintf(p, sizeof p, "out/lean_%d_%s.ppm", i, names[i]);
        write_ppm(p);
    }
    return 0;
}
