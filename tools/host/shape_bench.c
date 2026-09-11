#include <stdio.h>
#include <time.h>
#include <string.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
uint32_t esp_random(void) { return 1; }
static uint16_t band[466 * 32];
static void bench(const char *name, anim_sm_t *sm, eyes_t *eyes, anim_id_t id, float face) {
    raster_shape_t sh[2]; uint32_t t = 1000;
    anim_set(sm, eyes, id, t); eyes->idle.next_blink_ms = eyes->idle.next_dart_ms = t + 1000000;
    eyes_set_face_angle(eyes, face);
    for (; t < 3000; t += 16) { anim_update(sm, eyes, t); eyes_update(eyes, t, sh); }
    clock_t c0 = clock(); long px = 0;
    for (int it = 0; it < 2000; it++) for (int e = 0; e < 2; e++) {
        int w = sh[e].px1 - sh[e].px0;
        for (int y = sh[e].py0; y < sh[e].py1; y += 32) { int rows = sh[e].py1 - y < 32 ? sh[e].py1 - y : 32; raster_band(band, sh[e].px0, y, w, rows, sh, 2); px += w * rows; }
    }
    double us = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / 2000;
    printf("%-28s %6.1f us/frame  %6ld px  rot=%d bend=%d rad_eq=%d rad=%d,%d,%d,%d\n", name, us, px / 2000, sh[0].rot, sh[0].bend != 0, sh[0].rad_equal, sh[0].rad[0], sh[0].rad[1], sh[0].rad[2], sh[0].rad[3]);
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; eyes_init(&eyes, 1000); anim_init(&sm, &eyes, 1000);
    bench("neutral upright", &sm, &eyes, ANIM_NEUTRAL, 0.f);
    bench("happy (arc, radii)", &sm, &eyes, ANIM_HAPPY, 0.f);
    bench("sleepy (bend, upright)", &sm, &eyes, ANIM_SLEEPY, 0.f);
    bench("angry (angle, radii, bend)", &sm, &eyes, ANIM_ANGRY, 0.f);
    bench("sad (angle, bend)", &sm, &eyes, ANIM_SAD, 0.f);
    bench("neutral face 33 deg", &sm, &eyes, ANIM_NEUTRAL, 33.f);
    bench("angry face 33 deg", &sm, &eyes, ANIM_ANGRY, 33.f);
    eyes_set_hotspot(&eyes, true);
    bench("hearts shaded", &sm, &eyes, ANIM_HEARTS, 0.f);
    bench("broken hearts shaded 33 deg", &sm, &eyes, ANIM_HEARTBREAK, 33.f);
    bench("reels shaded", &sm, &eyes, ANIM_HIGH_ROLLER, 0.f);
    return 0;
}
