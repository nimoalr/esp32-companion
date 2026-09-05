/* Scenes with accessories and rotation, plus a behaviour simulation. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "raster.h"
#include "eyes.h"
#include "anim.h"
#include "accessories.h"
#include "behavior.h"
#include "board.h"
uint32_t esp_random(void) { return 0x12345678u; }
#define W 466
#define H 466
static uint16_t fb[W * H], band[W * 32];
static void render(const raster_shape_t *sh, const accessories_t *acc, uint32_t now) {
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        raster_band(band, 0, y, W, rows, sh, 2);
        gfx_band_t gb = { band, 0, y, W, rows };
        if (acc_any(acc)) acc_paint(acc, &gb, now);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
}
static void ppm(const char *p) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8)); unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 }; fwrite(px, 1, 3, f); }
    fclose(f);
}
static void settle(anim_sm_t *sm, eyes_t *e, raster_shape_t *sh, uint32_t *t, int ms) {
    for (uint32_t end = *t + ms; *t < end; *t += 16) { anim_update(sm, e, *t); eyes_update(e, *t, sh); }
}
int main(void) {
    eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; accessories_t acc; uint32_t t = 1000;
    eyes_init(&eyes, t); anim_init(&sm, &eyes, t); acc_init(&acc, 138, 328, 233);
    eyes.idle.next_blink_ms = t + 10000000; eyes.idle.next_dart_ms = t + 10000000;
    acc_rect_t ar[ACC_MAX_DIRTY];

    /* 1: neutral rotated 30 deg; 2: happy rotated 90 deg; 3: angry rotated -140 */
    eyes_set_face_angle(&eyes, 30.f); settle(&sm, &eyes, sh, &t, 400); render(sh, &acc, t); ppm("out/c_rot30.ppm");
    printf("rot30 bbox L[%d,%d)x[%d,%d)\n", sh[0].px0, sh[0].px1, sh[0].py0, sh[0].py1);
    anim_set(&sm, &eyes, ANIM_HAPPY, t); eyes_set_face_angle(&eyes, 90.f); settle(&sm, &eyes, sh, &t, 400); render(sh, &acc, t); ppm("out/c_rot90_happy.ppm");
    anim_set(&sm, &eyes, ANIM_ANGRY, t); eyes_set_face_angle(&eyes, -140.f); settle(&sm, &eyes, sh, &t, 400); render(sh, &acc, t); ppm("out/c_rotm140_angry.ppm");
    /* 4: dance with headphones, 20 deg */
    anim_set(&sm, &eyes, ANIM_DANCE, t); eyes_set_face_angle(&eyes, 20.f); acc_set_angle(&acc, 20.f); acc_set_headphones(&acc, true, t);
    audio_features_t af = { .active = true, .loud = 0.8f, .bass = 0.9f, .beat_count = 3, .last_beat_ms = t };
    anim_set_audio(&sm, &af);
    for (int i = 0; i < 40; i++) { t += 16; acc_update(&acc, t, ar); anim_update(&sm, &eyes, t); eyes_update(&eyes, t, sh); }
    render(sh, &acc, t); ppm("out/c_dance_headphones.ppm");
    /* 5: knocked out, upright */
    acc_set_headphones(&acc, false, t); eyes_set_face_angle(&eyes, 0.f); acc_set_angle(&acc, 0.f);
    for (int i = 0; i < 40; i++) { t += 16; acc_update(&acc, t, ar); }
    anim_set(&sm, &eyes, ANIM_SLEEPING, t); acc_set_knocked_out(&acc, true, t);
    settle(&sm, &eyes, sh, &t, 700); sh[0].visible = sh[1].visible = false; acc_update(&acc, t, ar); render(sh, &acc, t); ppm("out/c_ko.ppm");
    /* 6: sleeping face-down with zz (drawn upright here) */
    acc_set_knocked_out(&acc, false, t); acc_set_zz(&acc, true, t); settle(&sm, &eyes, sh, &t, 900); acc_update(&acc, t, ar); render(sh, &acc, t); ppm("out/c_zz.ppm");
    acc_set_zz(&acc, false, t);

    /* behaviour simulation */
    behavior_t b; behavior_out_t bo; behavior_init(&b, t);
    imu_cal_t cal; imu_cal_default(&cal);
    behavior_in_t in = { .have_accel = true, .cal = &cal, .mic_available = false };
    behavior_state_t last = -1;
    int16_t still[3] = { 0, 0, 4096 };
    for (int step = 0; step < 1500; step++) {   /* 50 ms steps, 75 s */
        t += 50;
        int16_t a[3] = { still[0], still[1], still[2] };
        if (step >= 40 && step < 140) {          /* 5 s of hard shaking: |a| swings 0.2..2.2 g */
            const float m = 1.f + 1.0f * sinf((float)step * 2.1f);
            a[2] = (int16_t)(4096.f * m);
        }
        if (step >= 700 && step < 800) { a[2] = -4096; }   /* 5 s face down */
        if (step >= 1000 && step < 1100) { a[0] = 2900; a[2] = 2900; }   /* tilted 45 deg right edge down */
        in.accel[0] = a[0]; in.accel[1] = a[1]; in.accel[2] = a[2]; in.accel_ms = t;
        behavior_update(&b, &in, t, &bo);
        if (bo.override_anim >= 0 || step == 1050 || step == 1099) {}
        if (b.state != last) { printf("t=%5.1fs state %-11s anim %d ko=%d zz=%d shake=%.2f\n", (t - 1000) / 1000.f, behavior_state_name(b.state), bo.override_anim, bo.knocked_out, bo.zz, b.shake); last = b.state; }
        if (step == 1050 || step == 1099 || step == 1150) printf("t=%5.1fs face angle %.1f deg, gaze dx %.1f px\n", (t - 1000) / 1000.f, bo.face_angle_deg, bo.env[0].dx / 65536.f);
    }
    /* music reaction rolls */
    int hp = 0, dance = 0, meh = 0;
    for (int trial = 0; trial < 200; trial++) {
        behavior_init(&b, t); b.energy = 0.6f; b.next_sniff_ms = t; in.mic_available = true;
        in.audio = (audio_features_t){ .active = true, .beat_count = 8, .bpm = 120, .regularity = 0.9f, .loud = 0.5f };
        for (int i = 0; i < 10; i++) { t += 100; behavior_update(&b, &in, t, &bo); }
        if (b.state == BEH_MUSIC) { dance++; if (bo.headphones) hp++; } else if (b.state == BEH_UNIMPRESSED) meh++;
        t += 1000;
    }
    printf("music rolls at energy 0.6: dance %d (headphones %d), unimpressed %d\n", dance, hp, meh);
    return 0;
}
