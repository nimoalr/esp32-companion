/* Cost of the rim charge gauge per eye band piece, relative to the eye raster itself. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "accessories.h"
#include "gfx.h"
#include "raster.h"
#include "eyes.h"
#include "anim.h"
uint32_t esp_random(void) { return 1; }
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
int main(void)
{
    static eyes_t eyes; static anim_sm_t sm; static raster_shape_t sh[2];
    eyes_init(&eyes, 0); anim_init(&sm, &eyes, 0); anim_set(&sm, &eyes, ANIM_NEUTRAL, 0);
    for (int t = 0; t < 600; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
    accessories_t acc; acc_init(&acc, 233 - 95, 233 + 95, 233);
    acc_set_charge(&acc, true, 80, true);
    static uint16_t band[466 * 16];
    double t_r = 0, t_a = 0; int pieces = 0;
    for (int rep = 0; rep < 200; rep++) {
        for (int e = 0; e < 2; e++) {
            const int x0 = sh[e].px0, w = sh[e].px1 - sh[e].px0;
            for (int y = sh[e].py0 - sh[e].py0 % 16; y < sh[e].py1; y += 16) {
                int y0 = y > sh[e].py0 ? y : sh[e].py0, y1 = y + 16 < sh[e].py1 ? y + 16 : sh[e].py1;
                double a = now_us(); raster_band(band, x0, y0, w, y1 - y0, sh, 2);
                double b = now_us();
                gfx_band_t gb = { .dst = band, .x0 = x0, .y0 = y0, .w = w, .rows = y1 - y0 };
                acc_paint(&acc, &gb, 0);
                double c = now_us();
                t_r += b - a; t_a += c - b; if (!rep) pieces++;
            }
        }
    }
    printf("%d pieces/frame: raster %.1f us/frame, ring paint %.1f us/frame (host)\n", pieces, t_r / 200, t_a / 200);
    return 0;
}
