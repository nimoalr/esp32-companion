/* Is a rotated shape's pixel bounding box ever too small? Render with the computed box and with the whole screen; diff. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
uint32_t esp_random(void) { return 1; }
static uint16_t fa[466 * 466], fb[466 * 466];
static void render(raster_shape_t *sh, uint16_t *fb)
{
    for (int y = 0; y < 466; y += 16) raster_band(fb + y * 466, 0, y, 466, (466 - y) < 16 ? (466 - y) : 16, sh, 2);
}
int main(void)
{
    static eyes_t eyes; static anim_sm_t sm; static raster_shape_t sh[2];
    eyes_init(&eyes, 0); anim_init(&sm, &eyes, 0);
    int bad = 0;
    for (int a = 0; a < ANIM_COUNT; a++) {
        anim_set(&sm, &eyes, (anim_id_t)a, 0);
        for (int deg = -90; deg <= 90; deg += 7) {
            eyes_set_face_angle(&eyes, (float)deg);
            for (int t = 0; t < 700; t += 16) { anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
            render(sh, fa);
            raster_shape_t big[2]; memcpy(big, sh, sizeof big);
            for (int e = 0; e < 2; e++) { big[e].px0 = 0; big[e].py0 = 0; big[e].px1 = 466; big[e].py1 = 466; }
            render(big, fb);
            int diff = 0, miny = 466, maxy = 0, minx = 466, maxx = 0;
            for (int i = 0; i < 466 * 466; i++) if (fa[i] != fb[i]) { diff++; int y = i / 466, x = i % 466; if (y < miny) miny = y; if (y > maxy) maxy = y; if (x < minx) minx = x; if (x > maxx) maxx = x; }
            if (diff) { bad++; printf("%-12s %4d deg: %6d px clipped, x %d..%d y %d..%d (box L %d..%d x %d..%d, R %d..%d x %d..%d)\n", anim_name((anim_id_t)a), deg, diff, minx, maxx, miny, maxy, sh[0].px0, sh[0].px1, sh[0].py0, sh[0].py1, sh[1].px0, sh[1].px1, sh[1].py0, sh[1].py1); }
        }
    }
    printf(bad ? "%d cases clipped\n" : "no clipping\n", bad);
    return 0;
}
