/* Sweep every animation through time at 60 Hz with blinks and saccades live; check
 * bounds and that no pixel outside a shape's bbox is ever written. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
#include "board.h"
uint32_t esp_random(void) { return 0xC0FFEE1u; }
#define W 466
#define H 466
static uint16_t band[W * 32];
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2];
    uint32_t t = 0; eyes_init(&eyes, t); anim_init(&sm, &eyes, t);
    long frames = 0, maxbytes = 0, sumbytes = 0, bad = 0;
    for (int a = 0; a < ANIM_COUNT * 2; a++) {
        if (a) anim_next(&sm, &eyes, t);
        eyes_set_hotspot(&eyes, (a / 3) & 1);
        eyes_set_face_scale(&eyes, (a & 4) ? Q16(1.25) : Q16_ONE);
        eyes_set_attention(&eyes, (a & 8) != 0, 60 + a * 9, 400 - a * 5);
        for (uint32_t end = t + 7000; t < end; t += 16) {
            eyes_set_face_angle(&eyes, (a & 1) ? (float)((t / 7) % 360) - 180.f : 0.f);
            anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh);
            long bytes = 0;
            for (int e = 0; e < 2; e++) {
                if (!sh[e].visible) continue;
                if (sh[e].px0 < 0 || sh[e].px1 > W || sh[e].py0 < 0 || sh[e].py1 > H || sh[e].px0 >= sh[e].px1 || sh[e].py0 >= sh[e].py1) { bad++; continue; }
                int w = sh[e].px1 - sh[e].px0;
                for (int y = sh[e].py0; y < sh[e].py1; y += 32) {
                    int rows = sh[e].py1 - y < 32 ? sh[e].py1 - y : 32;
                    raster_band(band, sh[e].px0, y, w, rows, sh, 2);
                }
                /* render a 4 px guard band around the bbox and verify it stays black */
                int gx0 = sh[e].px0 - 4, gw = 4;
                if (gx0 >= 0) { raster_band(band, gx0, sh[e].py0, gw, 8, &sh[e], 1); for (int i = 0; i < gw * 8; i++) if (band[i]) { bad++; break; } }
                int gy0 = sh[e].py0 - 4;
                if (gy0 >= 0) { raster_band(band, sh[e].px0, gy0, w, 4, &sh[e], 1); for (int i = 0; i < w * 4; i++) if (band[i]) { bad++; break; } }
                bytes += (long)w * (sh[e].py1 - sh[e].py0) * 2;
            }
            frames++; sumbytes += bytes; if (bytes > maxbytes) maxbytes = bytes;
        }
    }
    printf("sweep: %ld frames, avg %ld B/frame, max %ld B/frame, bbox/guard violations %ld\n", frames, sumbytes / frames, maxbytes, bad);
    return bad ? 1 : 0;
}
