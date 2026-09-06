/* Render every expression at several face angles to a raw file; build twice (old/new raster.c) and cmp. */
#include <stdio.h>
#include <string.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
uint32_t esp_random(void) { return 7; }
static uint16_t fb[466 * 466];
int main(int argc, char **argv)
{
    static eyes_t eyes; static anim_sm_t sm; static raster_shape_t sh[2];
    eyes_init(&eyes, 0); anim_init(&sm, &eyes, 0); eyes_set_hotspot(&eyes, true);
    FILE *f = fopen(argc > 1 ? argv[1] : "out/equiv.raw", "wb");
    for (int a = 0; a < ANIM_COUNT; a++) {
        anim_set(&sm, &eyes, (anim_id_t)a, 0);
        for (int deg = -40; deg <= 40; deg += 20) {
            eyes_set_face_angle(&eyes, (float)deg);
            for (int t = 0; t < 500; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
            for (int y = 0; y < 466; y += 16) raster_band(fb + y * 466, 0, y, 466, (466 - y) < 16 ? 466 - y : 16, sh, 2);
            fwrite(fb, 2, 466 * 466, f);
        }
    }
    fclose(f);
    return 0;
}
