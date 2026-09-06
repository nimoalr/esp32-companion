/* Shake/recovery integration, authored eyelids, and accessory dirty coverage. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "behavior.h"
#include "accessories.h"

uint32_t esp_random(void) { return 0x12345678u; }
static uint16_t before[466*466], after[466*466];

static double render_symbol(eye_symbol_t symbol, int32_t reel, float angle)
{
    uint16_t lut[256];
    raster_build_lut(lut, 255, 140, 0);
    raster_shape_t shape = { .visible = true, .cx = Q16(233), .cy = Q16(233),
        .hw = Q16(62), .hh = Q16(76), .lut = lut,
        .rc = (int32_t)(cosf(angle)*Q16_ONE), .rs = (int32_t)(sinf(angle)*Q16_ONE) };
    eye_symbol_shape(&shape, symbol, Q16(0.2), reel);
    raster_shape_finalize(&shape, 466, 466);
    for (int y = 0; y < 466; y += 16)
        raster_band(after+y*466, 0, y, 466, 466-y < 16 ? 466-y : 16, &shape, 1);
    int count = 0; double sum_y = 0;
    for (int y = 0; y < 466; y++) for (int x = 0; x < 466; x++) {
        if (!after[y*466+x]) continue;
        assert(x >= shape.px0 && x < shape.px1 && y >= shape.py0 && y < shape.py1);
        sum_y += y; count++;
    }
    assert(count > 300);
    return sum_y/count;
}

static void render_props(accessories_t *a, uint32_t t, uint16_t *fb)
{
    memset(fb, 0, sizeof before);
    for (int y = 0; y < 466; y += 16) {
        const gfx_band_t band = { fb+y*466, 0, y, 466, 466-y < 16 ? 466-y : 16 };
        acc_paint(a, &band, t);
    }
}

int main(void)
{
    assert(ANIM_DANCE == 22 && ANIM_SMUG == 23); /* Existing selection IDs stay stable. */
    for (int i = 0; i < ANIM_COUNT; i++) {
        assert(anim_name((anim_id_t)i) && strcmp(anim_name((anim_id_t)i), "?"));
        for (int j = 0; j < i; j++) assert(strcmp(anim_name((anim_id_t)i), anim_name((anim_id_t)j)));
    }
    eyes_t eyes;
    anim_sm_t anim = {0};
    raster_shape_t shapes[2];
    eyes_init(&eyes, 1000); anim_init(&anim, &eyes, 1000);
    behavior_t beh; behavior_init(&beh, 1000);
    imu_cal_t cal; imu_cal_default(&cal);
    behavior_in_t in = { .have_accel = true, .cal = &cal };
    behavior_out_t out;
    bool dizzy = false, ko = false, groggy = false, recovered = false;
    uint32_t ko_at = 0, groggy_at = 0;
    for (uint32_t t = 1016; t < 22000; t += 16) {
        float magnitude = 1.f;
        if (t >= 2000 && t < 6700) magnitude += sinf((float)t * 0.035f);
        in.accel[2] = (int16_t)(4096.f * magnitude); in.accel_ms = t;
        behavior_update(&beh, &in, t, &out);
        const anim_id_t want = out.override_anim < 0 ? ANIM_NEUTRAL : (anim_id_t)out.override_anim;
        if (anim.id != want) anim_set(&anim, &eyes, want, t);
        anim_update(&anim, &eyes, t); eyes_update(&eyes, t, shapes);
        if (beh.state == BEH_DIZZY) dizzy = true;
        if (beh.state == BEH_KNOCKED_OUT) {
            if (!ko) ko_at = t;
            ko = true;
            assert(dizzy && out.knocked_out && !out.zz && want == ANIM_KNOCKED_OUT);
            assert(shapes[0].visible && shapes[1].visible && !eyes.idle.blinking);
        }
        if (beh.state == BEH_GROGGY) {
            if (!groggy) {
                groggy_at = t;
                assert(t-ko_at >= 8000 && t-ko_at < 8016);
            }
            groggy = true;
            assert(ko && !out.knocked_out && !out.zz && want == ANIM_RECOVERING);
            assert(shapes[0].visible && shapes[1].visible);
        }
        if (groggy && beh.state == BEH_IDLE && !recovered) {
            assert(t-groggy_at >= 3000 && t-groggy_at < 3016);
            recovered = true;
            assert(want == ANIM_NEUTRAL && eyes.idle.blink_interval_scale != 0);
        }
    }
    assert(dizzy && ko && groggy && recovered);

    /* The authored collapse cancels a blink already in progress. */
    eyes_blink_now(&eyes, 23000);
    anim_set(&anim, &eyes, ANIM_KNOCKED_OUT, 23000);
    eyes_update(&eyes, 23016, shapes);
    assert(!eyes.idle.blinking);
    anim_set(&anim, &eyes, ANIM_RECOVERING, 24000);
    for (uint32_t t = 24000; t <= 27200; t += 16) {
        anim_update(&anim, &eyes, t); eyes_update(&eyes, t, shapes);
    }
    assert(eyes.idle.blink_interval_scale == Q16_ONE); /* Also alive when selected manually. */
    eyes_set_idle_rates(&eyes, Q16(20), Q16_ONE, 0);
    eyes_blink_now(&eyes, 28000); eyes_update(&eyes, 28350, shapes);
    assert(eyes.idle.next_blink_ms-28350 >= 60000 && eyes.idle.next_blink_ms-28350 <= 200000);

    /* Render concave/disconnected contours through a full turn, not only their
     * bounding boxes. Scrolling must move the symbol inside a stationary window. */
    for (int symbol = EYE_SYMBOL_HEART; symbol <= EYE_SYMBOL_REEL; symbol++)
        for (int degree = 0; degree < 360; degree += 15)
            for (int phase = 0; phase < 4; phase++)
                render_symbol((eye_symbol_t)symbol, phase*Q16_ONE/4, degree*3.14159265f/180.f);
    const double centered = render_symbol(EYE_SYMBOL_REEL, 0, 0);
    const double scrolling = render_symbol(EYE_SYMBOL_REEL, Q16(0.1), 0);
    assert(scrolling > centered+8);

    anim_set(&anim, &eyes, ANIM_HIGH_ROLLER, 30000);
    for (uint32_t t = 30000; t <= 32800; t += 16) {
        anim_update(&anim, &eyes, t); eyes_update(&eyes, t, shapes);
    }
    const int32_t stopped = eyes.reel_pos[0], spinning = eyes.reel_pos[1];
    anim_update(&anim, &eyes, 32900); eyes_update(&eyes, 32900, shapes);
    assert(eyes.reel_pos[0] == stopped && eyes.reel_pos[1] > spinning);
    anim_set(&anim, &eyes, ANIM_NEUTRAL, 33000);
    assert(eyes.symbol[0] == EYE_SYMBOL_REEL && eyes.reel_pos[0] == stopped);
    anim_update(&anim, &eyes, 33120);
    assert(eyes.symbol[0] == EYE_SYMBOL_NONE && eyes.shape_gate[0] == 0);
    anim_update(&anim, &eyes, 33300);
    assert(eyes.symbol[0] == EYE_SYMBOL_NONE && eyes.shape_gate[0] == Q16_ONE);
    anim_set(&anim, &eyes, ANIM_HEARTS, 34000);
    anim_update(&anim, &eyes, 34064);
    const int32_t interrupted_gate = eyes.shape_gate[0];
    anim_set(&anim, &eyes, ANIM_NEUTRAL, 34064);
    assert(eyes.shape_gate[0] == interrupted_gate); /* Rapid tapping cannot pop it open. */
    memset(before, 0, sizeof before);

    /* Every changed star pixel must be repainted, including disable while rotated. */
    accessories_t acc; acc_init(&acc, 138, 328, 233);
    acc_rect_t rects[ACC_MAX_DIRTY];
    for (uint32_t step = 0; step < 70; step++) {
        const uint32_t t = step*50;
        if (step == 1 || step == 42) acc_set_knocked_out(&acc, true, t);
        if (step == 30 || step == 62) acc_set_knocked_out(&acc, false, t);
        if (step >= 42) acc_set_angle(&acc, (float)(step-42)*11.f);
        const int n = acc_update(&acc, t, rects);
        render_props(&acc, t, after);
        for (int y = 0; y < 466; y++) for (int x = 0; x < 466; x++) {
            if (before[y*466+x] == after[y*466+x]) continue;
            bool covered = false;
            for (int i = 0; i < n; i++) {
                if (x >= rects[i].x0 && x < rects[i].x1 && y >= rects[i].y0 && y < rects[i].y1) covered = true;
            }
            assert(covered);
        }
        memcpy(before, after, sizeof before);
    }
    puts("PASS: stable IDs; shake -> collapse -> 8 s hold -> 3 s recovery -> idle; authored blink; long blink gap; rotated symbols; scrolling/staggered reels; silhouette transition; star dirty coverage");
    return 0;
}
