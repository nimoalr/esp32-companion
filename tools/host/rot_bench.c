#include <stdio.h>
#include <time.h>
#include <string.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
uint32_t esp_random(void) { return 1; }
static uint16_t band[466 * 32];
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t);
    for (int rot = 0; rot < 2; rot++) {
        eyes_set_face_angle(&eyes, rot ? 33.f : 0.f);
        anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh);
        clock_t c0 = clock(); long px = 0;
        for (int it = 0; it < 2000; it++) for (int e = 0; e < 2; e++) {
            int w = sh[e].px1 - sh[e].px0;
            for (int y = sh[e].py0; y < sh[e].py1; y += 32) { int rows = sh[e].py1 - y < 32 ? sh[e].py1 - y : 32; raster_band(band, sh[e].px0, y, w, rows, sh, 2); px += w * rows; }
        }
        double us = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / 2000;
        printf("%s: %.1f us/frame on host, %ld px/frame\n", rot ? "rotated 33 deg" : "upright", us, px / 2000);
    }
    return 0;
}
