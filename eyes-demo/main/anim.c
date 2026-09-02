#include "anim.h"

#include <stddef.h>
#include "esp_log.h"

static const char *TAG = "anim";

/*
 * Every animation is a keyframe list. A keyframe carries a pose for each eye
 * and the easing time used to reach it; keyframes fire when `t_ms` has elapsed
 * since the animation was entered (or since the last loop wrap).
 */
typedef struct {
    uint32_t t_ms;
    uint32_t ease_ms;
    eye_pose_t left;
    eye_pose_t right;
} anim_kf_t;

typedef struct {
    const char *name;
    const anim_kf_t *kf;
    int nkf;
    uint32_t loop_ms;               /* 0 = play once and hold the last keyframe */
    int32_t blink_interval_scale;   /* Q16 */
    int32_t blink_speed_scale;      /* Q16 */
} anim_def_t;

/* pose: sx, sy, lid_top, lid_bottom, slant, curve, dx, dy (all floats -> Q16 at compile time) */
#define P(sx, sy, lt, lb, sl, cv, dx, dy) \
    { { Q16(sx), Q16(sy), Q16(lt), Q16(lb), Q16(sl), Q16(cv), Q16(dx), Q16(dy) } }

#define NEUTRAL_POSE P(1.00, 1.00, 0.00, 0.00, 0.00, 0.00, 0, 0)

static const anim_kf_t kf_neutral[] = {
    { 0, 250, NEUTRAL_POSE, NEUTRAL_POSE },
};

/* Bottom lids arc upward, eyes a little squashed and lifted by the "cheeks". */
static const anim_kf_t kf_happy[] = {
    { 0, 250, P(1.06, 0.88, 0.00, 0.04, 0.00, 0.52, 0, 3), P(1.06, 0.88, 0.00, 0.04, 0.00, 0.52, 0, 3) },
};

/* Top lids slope down toward the outer edges; the eyes then drift downward. */
static const anim_kf_t kf_sad[] = {
    { 0,   250, P(1.00, 0.95, 0.14, 0.00, -0.38, 0.00, 0, 2), P(1.00, 0.95, 0.14, 0.00, 0.38, 0.00, 0, 2) },
    { 250, 900, P(1.00, 0.95, 0.14, 0.00, -0.38, 0.00, 0, 10), P(1.00, 0.95, 0.14, 0.00, 0.38, 0.00, 0, 10) },
};

/* Top lids slope down toward the inner edges; eyes narrowed. */
static const anim_kf_t kf_angry[] = {
    { 0, 250, P(1.00, 0.74, 0.20, 0.04, 0.42, 0.00, 0, 0), P(1.00, 0.74, 0.20, 0.04, -0.42, 0.00, 0, 0) },
};

/* Quick pop to 130 %. */
static const anim_kf_t kf_surprised[] = {
    { 0, 120, P(1.30, 1.30, 0.00, 0.00, 0.00, 0.00, 0, 0), P(1.30, 1.30, 0.00, 0.00, 0.00, 0.00, 0, 0) },
};

/* Lids sag to ~30 % open, slow vertical drift. */
#define SLEEPY(dy) P(1.00, 1.00, 0.55, 0.15, 0.00, 0.00, 0, dy)
static const anim_kf_t kf_sleepy[] = {
    { 0,    300,  SLEEPY(2), SLEEPY(2) },
    { 300,  2500, SLEEPY(6), SLEEPY(6) },
    { 2800, 2500, SLEEPY(1), SLEEPY(1) },
};

/* Scripted: look left, hold, look right, hold, look up, return. */
#define LOOK(dx, dy) P(1.00, 1.00, 0.00, 0.00, 0.00, 0.00, dx, dy)
static const anim_kf_t kf_look_around[] = {
    { 0,    260, LOOK(-16, 0),  LOOK(-16, 0) },
    { 900,  300, LOOK(16, 0),   LOOK(16, 0) },
    { 1800, 300, LOOK(0, -14),  LOOK(0, -14) },
    { 2700, 300, LOOK(0, 0),    LOOK(0, 0) },
};

/* Right eye closes and reopens, then both stay neutral until the loop repeats. */
#define CLOSED_POSE P(1.00, 0.03, 0.00, 0.00, 0.00, 0.00, 0, 0)
static const anim_kf_t kf_wink[] = {
    { 0,   250, NEUTRAL_POSE, NEUTRAL_POSE },
    { 500, 90,  NEUTRAL_POSE, CLOSED_POSE },
    { 800, 120, NEUTRAL_POSE, NEUTRAL_POSE },
};

#define DEF(nm, arr, loop, bi, bs) \
    { nm, arr, (int)(sizeof(arr) / sizeof(arr[0])), loop, Q16(bi), Q16(bs) }

static const anim_def_t k_anims[ANIM_COUNT] = {
    [ANIM_NEUTRAL]     = DEF("NEUTRAL",     kf_neutral,     0,    1.0, 1.0),
    [ANIM_HAPPY]       = DEF("HAPPY",       kf_happy,       0,    1.0, 1.0),
    [ANIM_SAD]         = DEF("SAD",         kf_sad,         0,    1.3, 1.3),
    [ANIM_ANGRY]       = DEF("ANGRY",       kf_angry,       0,    1.0, 1.0),
    [ANIM_SURPRISED]   = DEF("SURPRISED",   kf_surprised,   0,    1.6, 1.0),
    [ANIM_SLEEPY]      = DEF("SLEEPY",      kf_sleepy,      5300, 2.5, 2.2),
    [ANIM_LOOK_AROUND] = DEF("LOOK_AROUND", kf_look_around, 3600, 1.0, 1.0),
    [ANIM_WINK]        = DEF("WINK",        kf_wink,        3500, 1.0, 1.0),
};

const char *anim_name(anim_id_t id)
{
    return (id < ANIM_COUNT) ? k_anims[id].name : "?";
}

static void anim_enter(anim_sm_t *sm, eyes_t *eyes, anim_id_t id, uint32_t now_ms)
{
    const anim_def_t *d = &k_anims[id];
    sm->id = id;
    sm->t_enter_ms = now_ms;
    sm->next_kf = 0;
    eyes_set_idle_rates(eyes, d->blink_interval_scale, d->blink_speed_scale);
    ESP_LOGI(TAG, "-> %s", d->name);
}

void anim_init(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    anim_enter(sm, eyes, ANIM_NEUTRAL, now_ms);
    anim_update(sm, eyes, now_ms);
}

void anim_next(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    anim_enter(sm, eyes, (anim_id_t)((sm->id + 1) % ANIM_COUNT), now_ms);
    anim_update(sm, eyes, now_ms);
}

void anim_update(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    const anim_def_t *d = &k_anims[sm->id];
    uint32_t el = now_ms - sm->t_enter_ms;

    if (d->loop_ms && el >= d->loop_ms) {
        sm->t_enter_ms += d->loop_ms;
        sm->next_kf = 0;
        el -= d->loop_ms;
    }
    while (sm->next_kf < d->nkf && d->kf[sm->next_kf].t_ms <= el) {
        const anim_kf_t *k = &d->kf[sm->next_kf];
        eyes_set_target(eyes, 0, &k->left, k->ease_ms, now_ms);
        eyes_set_target(eyes, 1, &k->right, k->ease_ms, now_ms);
        sm->next_kf++;
    }
}
