#include "anim.h"

#include <stddef.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "anim";

/*
 * Every animation is a keyframe list plus up to three modulators. A keyframe
 * carries a pose for each eye and the easing time used to reach it; keyframes
 * fire when `t_ms` has elapsed since the animation was entered (or since the
 * last loop wrap). Modulators add a periodic or random delta to one pose
 * field every frame, on top of the eased pose.
 */
typedef struct {
    uint32_t t_ms;
    uint32_t ease_ms;
    eye_pose_t left;
    eye_pose_t right;
    uint8_t ease;           /* eye_ease_t; SNAP = fast with a small overshoot */
} anim_kf_t;

typedef enum { MOD_NONE = 0, MOD_SINE, MOD_JITTER } mod_kind_t;

typedef struct {
    mod_kind_t kind;
    uint8_t field;          /* index into eye_pose_t.v */
    uint8_t eyes;           /* bit0 left, bit1 right */
    int32_t amp;            /* Q16 */
    uint32_t period_ms;     /* SINE period, or JITTER retarget interval */
    int16_t phase_l, phase_r;   /* degrees */
} anim_mod_t;

typedef struct {
    const char *name;
    const anim_kf_t *kf;
    int nkf;
    uint32_t loop_ms;               /* 0 = play once and hold the last keyframe */
    int32_t blink_interval_scale;   /* Q16 */
    int32_t blink_speed_scale;      /* Q16 */
    int32_t dart_scale;             /* Q16 amplitude of the idle gaze darts */
    anim_mod_t mods[3];
} anim_def_t;

/* pose fields */
enum { F_SX, F_SY, F_LT, F_LB, F_SL, F_CV, F_DX, F_DY, F_ANG, F_BEND, F_RTL, F_RTR, F_RBL, F_RBR };
#define BOTH 3
#define LEFT 1
#define RIGHT 2

/*
 * pose: sx, sy, lid_top, lid_bottom, slant, curve, dx, dy, then (PX only) the
 * eye angle in degrees, the top lid bend and the four corner radius scales
 * TL, TR, BL, BR (floats -> Q16 at compile time)
 */
#define PX(sx, sy, lt, lb, sl, cv, dx, dy, ang, bend, rtl, rtr, rbl, rbr) \
    { { Q16(sx), Q16(sy), Q16(lt), Q16(lb), Q16(sl), Q16(cv), Q16(dx), Q16(dy), Q16(ang), Q16(bend), \
        { Q16(rtl), Q16(rtr), Q16(rbl), Q16(rbr) } } }
#define P(sx, sy, lt, lb, sl, cv, dx, dy) PX(sx, sy, lt, lb, sl, cv, dx, dy, 0, 0, 1, 1, 1, 1)
#define SNAP EYE_EASE_SNAP
#define NEUTRAL_POSE P(1.00, 1.00, 0.00, 0.00, 0.00, 0.00, 0, 0)
#define SINE(f, e, a, per, pl, pr)   { MOD_SINE, f, e, Q16(a), per, pl, pr }
#define JITTER(f, e, a, per)         { MOD_JITTER, f, e, Q16(a), per, 0, 0 }
#define NOMOD                        { MOD_NONE, 0, 0, 0, 0, 0, 0 }

/* ---- the original eight --------------------------------------------------- */

static const anim_kf_t kf_neutral[] = {
    { 0, 250, NEUTRAL_POSE, NEUTRAL_POSE, 0 },
};
#define HAPPY_POSE PX(1.06, 0.88, 0.00, 0.04, 0.00, 0.52, 0, 3, 0, 0, 1.15, 1.15, 0.55, 0.55)
static const anim_kf_t kf_happy[] = {
    { 0, 250, HAPPY_POSE, HAPPY_POSE, SNAP },
};
#define SAD_L(dy) PX(1.00, 0.95, 0.12, 0.00, -0.30, 0.00, 0, dy, -9, 0.10, 1.2, 1.2, 1.0, 1.0)
#define SAD_R(dy) PX(1.00, 0.95, 0.12, 0.00, 0.30, 0.00, 0, dy, 9, 0.10, 1.2, 1.2, 1.0, 1.0)
static const anim_kf_t kf_sad[] = {
    { 0,   250, SAD_L(2),  SAD_R(2), 0 },
    { 250, 900, SAD_L(10), SAD_R(10), 0 },
};
static const anim_kf_t kf_angry[] = {
    { 0, 220, PX(1.00, 0.74, 0.18, 0.04, 0.34, 0.00, 2, 0, 7, -0.06, 1.0, 0.25, 1.0, 0.7),
              PX(1.00, 0.74, 0.18, 0.04, -0.34, 0.00, -2, 0, -7, -0.06, 0.25, 1.0, 0.7, 1.0), SNAP },
};
#define SURPRISED_POSE PX(1.30, 1.30, 0.00, 0.00, 0.00, 0.00, 0, -2, 0, 0, 1.3, 1.3, 1.3, 1.3)
static const anim_kf_t kf_surprised[] = {
    { 0,  60,  P(0.96, 0.92, 0.00, 0.00, 0.00, 0.00, 0, 2), P(0.96, 0.92, 0.00, 0.00, 0.00, 0.00, 0, 2), 0 },
    { 60, 150, SURPRISED_POSE, SURPRISED_POSE, SNAP },
};
#define SLEEPY(dy) PX(1.00, 1.00, 0.50, 0.15, 0.00, 0.00, 0, dy, 0, 0.16, 1, 1, 1, 1)
static const anim_kf_t kf_sleepy[] = {
    { 0,    300,  SLEEPY(2), SLEEPY(2), 0 },
    { 300,  2500, SLEEPY(6), SLEEPY(6), 0 },
    { 2800, 2500, SLEEPY(1), SLEEPY(1), 0 },
};
#define LOOK(dx, dy) P(1.00, 1.00, 0.00, 0.00, 0.00, 0.00, dx, dy)
static const anim_kf_t kf_look_around[] = {
    { 0,    260, LOOK(-16, 0),  LOOK(-16, 0), 0 },
    { 900,  300, LOOK(16, 0),   LOOK(16, 0), 0 },
    { 1800, 300, LOOK(0, -14),  LOOK(0, -14), 0 },
    { 2700, 300, LOOK(0, 0),    LOOK(0, 0), 0 },
};
#define CLOSED_POSE P(1.25, 0.03, 0.00, 0.00, 0.00, 0.00, 0, 0)
static const anim_kf_t kf_wink[] = {
    { 0,   250, NEUTRAL_POSE, NEUTRAL_POSE, 0 },
    { 500, 110, PX(1.00, 1.04, 0.00, 0.00, 0.00, 0.10, 0, 0, 0, 0, 1, 1, 1, 1), CLOSED_POSE, 0 },
    { 800, 170, NEUTRAL_POSE, NEUTRAL_POSE, 0 },
};

/* ---- new expressions -------------------------------------------------------- */

/* One eye wide and lifted, the other squinting: "hm?" */
static const anim_kf_t kf_curious[] = {
    { 0, 280, PX(1.10, 1.16, 0.00, 0.00, 0.00, 0.00, -3, -8, -6, 0, 1.25, 1.25, 1.25, 1.25),
              PX(0.98, 0.70, 0.26, 0.04, -0.30, 0.00, -3, 4, 0, 0.08, 1, 1, 0.8, 0.8), SNAP },
};
/* Lids at odds, gaze drifting side to side. */
static const anim_kf_t kf_confused[] = {
    { 0, 280, PX(1.00, 1.00, 0.16, 0.00, 0.32, 0.00, 0, 0, 0, 0.06, 1, 1, 1, 1),
              PX(1.00, 1.05, 0.00, 0.00, 0.00, 0.00, 0, 0, 12, 0, 1.15, 1.15, 1.15, 1.15), 0 },
};
/* Tall, soft eyes with a heartbeat. */
#define LOVE_POSE PX(1.05, 1.15, 0.00, 0.00, 0.00, 0.12, 0, -2, 0, -0.05, 1.3, 1.3, 1.3, 1.3)
static const anim_kf_t kf_love[] = {
    { 0, 300, LOVE_POSE, LOVE_POSE, SNAP },
};
/* Half lids, eyes circling in opposite directions. */
static const anim_kf_t kf_dizzy[] = {
    { 0, 300, P(1.00, 0.95, 0.22, 0.14, 0.00, 0.00, 0, 0), P(1.00, 0.95, 0.22, 0.14, 0.00, 0.00, 0, 0), 0 },
};
/* Happy arcs bouncing. */
#define LAUGH_POSE PX(1.08, 0.80, 0.00, 0.04, 0.00, 0.56, 0, 4, 0, 0, 1.2, 1.2, 0.45, 0.45)
static const anim_kf_t kf_laughing[] = {
    { 0, 220, LAUGH_POSE, LAUGH_POSE, SNAP },
};
/* Wide, high and trembling. */
#define SCARED_POSE PX(1.18, 1.26, 0.00, 0.00, 0.00, 0.00, 0, -9, 0, -0.04, 1.3, 1.3, 1.3, 1.3)
static const anim_kf_t kf_scared[] = {
    { 0, 140, SCARED_POSE, SCARED_POSE, SNAP },
};
/* One flat lid, the other eye open: raised eyebrow. */
static const anim_kf_t kf_skeptical[] = {
    { 0, 260, PX(1.00, 1.00, 0.42, 0.00, 0.00, 0.00, 4, 0, 0, 0.04, 0.6, 0.6, 1, 1),
              PX(1.02, 1.10, 0.00, 0.00, 0.00, 0.00, 4, -3, -4, -0.12, 1.2, 1.2, 1.2, 1.2), 0 },
};
/* Looking up and away, lids a little heavy. */
static const anim_kf_t kf_thinking[] = {
    { 0,    300, P(1.00, 1.00, 0.20, 0.00, 0.00, 0.00, -10, -12), P(1.00, 1.00, 0.20, 0.00, 0.00, 0.00, -10, -12), 0 },
    { 2600, 400, P(1.00, 1.00, 0.20, 0.00, 0.00, 0.00, 8, -12),   P(1.00, 1.00, 0.20, 0.00, 0.00, 0.00, 8, -12), 0 },
};
/* Heavy lids, slow gaze from side to side with long holds. */
#define BORED(dx) PX(1.00, 1.00, 0.34, 0.00, 0.00, 0.00, dx, 4, 0, 0.12, 0.7, 0.7, 1, 1)
static const anim_kf_t kf_bored[] = {
    { 0,    800, BORED(-10), BORED(-10), 0 },
    { 3000, 800, BORED(10),  BORED(10), 0 },
};
/* Big eyes jumping. */
#define EXCITED_POSE PX(1.15, 1.20, 0.00, 0.00, 0.00, 0.10, 0, -4, 0, 0, 1.2, 1.2, 1.2, 1.2)
static const anim_kf_t kf_excited[] = {
    { 0, 160, EXCITED_POSE, EXCITED_POSE, SNAP },
};
/* Looking down, occasional glance up. */
#define SHY(dy) P(1.00, 0.86, 0.14, 0.00, 0.00, 0.00, 6, dy)
static const anim_kf_t kf_shy[] = {
    { 0,    300, SHY(14), SHY(14), 0 },
    { 2500, 300, SHY(-2), SHY(-2), 0 },
    { 3200, 300, SHY(14), SHY(14), 0 },
};
/* Flat lids and an eye-roll. */
#define ANN(dx, dy) PX(1.00, 1.00, 0.38, 0.00, 0.00, 0.00, dx, dy, 0, 0.10, 0.6, 0.6, 1, 1)
static const anim_kf_t kf_annoyed[] = {
    { 0,    300, ANN(0, 0),     ANN(0, 0), 0 },
    { 700,  260, ANN(12, 0),    ANN(12, 0), 0 },
    { 960,  260, ANN(9, -11),   ANN(9, -11), 0 },
    { 1220, 260, ANN(0, -13),   ANN(0, -13), 0 },
    { 1480, 260, ANN(-9, -9),   ANN(-9, -9), 0 },
    { 1740, 300, ANN(0, 0),     ANN(0, 0), 0 },
};
/* Closed, breathing slowly. */
static const anim_kf_t kf_sleeping[] = {
    { 0, 600, P(1.00, 0.06, 0.00, 0.00, 0.00, 0.00, 0, 6), P(1.00, 0.06, 0.00, 0.00, 0.00, 0.00, 0, 6), 0 },
};
/* Dance: the pose is neutral, everything happens in the modulation layer. */
static const anim_kf_t kf_dance[] = {
    { 0, 250, NEUTRAL_POSE, NEUTRAL_POSE, 0 },
};

/* name, keyframes, loop ms, blink interval scale, blink speed scale, dart amplitude scale, modulators */
#define DEF(nm, arr, loop, bi, bs, ds, ...) \
    { nm, arr, (int)(sizeof(arr) / sizeof(arr[0])), loop, Q16(bi), Q16(bs), Q16(ds), { __VA_ARGS__ } }

static const anim_def_t k_anims[ANIM_COUNT] = {
    [ANIM_NEUTRAL]     = DEF("NEUTRAL",     kf_neutral,     0,    1.0, 1.0, 1.0, NOMOD),
    [ANIM_HAPPY]       = DEF("HAPPY",       kf_happy,       0,    1.0, 1.0, 0.9, NOMOD),
    [ANIM_SAD]         = DEF("SAD",         kf_sad,         0,    1.3, 1.3, 0.5, NOMOD),
    [ANIM_ANGRY]       = DEF("ANGRY",       kf_angry,       0,    1.0, 1.0, 0.6, NOMOD),
    [ANIM_SURPRISED]   = DEF("SURPRISED",   kf_surprised,   0,    1.6, 1.0, 0.3, NOMOD),
    [ANIM_SLEEPY]      = DEF("SLEEPY",      kf_sleepy,      5300, 2.5, 2.2, 0.4, NOMOD),
    [ANIM_LOOK_AROUND] = DEF("LOOK_AROUND", kf_look_around, 3600, 1.0, 1.0, 0.5, NOMOD),
    [ANIM_WINK]        = DEF("WINK",        kf_wink,        3500, 1.0, 1.0, 0.8, NOMOD),
    [ANIM_CURIOUS]     = DEF("CURIOUS",     kf_curious,     0,    1.2, 1.0, 0.7,
                             SINE(F_DY, LEFT, 3.0, 1500, 0, 0)),
    [ANIM_CONFUSED]    = DEF("CONFUSED",    kf_confused,    0,    1.0, 1.0, 0.8,
                             SINE(F_DX, BOTH, 8.0, 1600, 0, 0), SINE(F_DY, RIGHT, 4.0, 900, 0, 90),
                             SINE(F_ANG, RIGHT, 4.0, 1600, 90, 90)),
    [ANIM_LOVE]        = DEF("LOVE",        kf_love,        0,    1.4, 1.2, 0.5,
                             SINE(F_SY, BOTH, 0.07, 700, 0, 0), SINE(F_SX, BOTH, 0.05, 700, 0, 0)),
    [ANIM_DIZZY]       = DEF("DIZZY",       kf_dizzy,       0,    2.0, 1.5, 0.0,
                             SINE(F_DX, BOTH, 10.0, 900, 0, 180), SINE(F_DY, BOTH, 10.0, 900, 90, 270),
                             SINE(F_ANG, BOTH, 14.0, 900, 180, 0)),
    [ANIM_LAUGHING]    = DEF("LAUGHING",    kf_laughing,    0,    1.5, 1.0, 0.5,
                             SINE(F_DY, BOTH, 5.0, 260, 0, 0), SINE(F_SY, BOTH, 0.05, 260, 90, 90)),
    [ANIM_SCARED]      = DEF("SCARED",      kf_scared,      0,    3.0, 0.6, 1.4,
                             JITTER(F_DX, BOTH, 3.0, 50), JITTER(F_DY, BOTH, 3.0, 50)),
    [ANIM_SKEPTICAL]   = DEF("SKEPTICAL",   kf_skeptical,   0,    1.0, 1.0, 0.6, NOMOD),
    [ANIM_THINKING]    = DEF("THINKING",    kf_thinking,    5200, 1.3, 1.3, 0.6,
                             SINE(F_DX, BOTH, 2.0, 2500, 0, 0)),
    [ANIM_BORED]       = DEF("BORED",       kf_bored,       6000, 1.6, 1.8, 0.5, NOMOD),
    [ANIM_EXCITED]     = DEF("EXCITED",     kf_excited,     0,    0.7, 0.8, 1.2,
                             SINE(F_DY, BOTH, 8.0, 320, 0, 0), SINE(F_SX, BOTH, 0.04, 320, 90, 90)),
    [ANIM_SHY]         = DEF("SHY",         kf_shy,         5000, 1.0, 1.0, 0.6, NOMOD),
    [ANIM_ANNOYED]     = DEF("ANNOYED",     kf_annoyed,     4500, 1.0, 1.0, 0.5, NOMOD),
    [ANIM_SLEEPING]    = DEF("SLEEPING",    kf_sleeping,    0,    20.0, 3.0, 0.0,
                             SINE(F_DY, BOTH, 2.0, 3200, 0, 0), SINE(F_SX, BOTH, 0.02, 3200, 0, 0)),
    [ANIM_DANCE]       = DEF("DANCE",       kf_dance,       0,    1.0, 1.0, 0.5, NOMOD),
};

const char *anim_name(anim_id_t id)
{
    return (id < ANIM_COUNT) ? k_anims[id].name : "?";
}

static uint32_t rng_next(anim_sm_t *sm)
{
    uint32_t x = sm->rng ? sm->rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    sm->rng = x;
    return x;
}

static void anim_enter(anim_sm_t *sm, eyes_t *eyes, anim_id_t id, uint32_t now_ms)
{
    const anim_def_t *d = &k_anims[id];
    sm->id = id;
    sm->t_enter_ms = now_ms;
    sm->next_kf = 0;
    sm->jit_t0_ms = now_ms;
    for (int e = 0; e < 2; e++) {
        for (int f = 0; f < EYE_POSE_FIELDS; f++) {
            sm->jit_from[e][f] = sm->jit_to[e][f] = 0;
        }
    }
    sm->dance_beats_seen = sm->audio.beat_count;
    sm->dance_bass = sm->dance_loud = sm->dance_bal = 0.f;
    sm->dance_side = 1;
    sm->dance_last_sound_ms = now_ms;
    eyes_clear_mod(eyes);
    eyes_set_idle_rates(eyes, d->blink_interval_scale, d->blink_speed_scale, d->dart_scale);
    ESP_LOGI(TAG, "-> %s", d->name);
}

void anim_init(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    sm->rng = 0xA5A5F00Du ^ now_ms;
    anim_enter(sm, eyes, ANIM_NEUTRAL, now_ms);
    anim_update(sm, eyes, now_ms);
}

void anim_next(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    anim_enter(sm, eyes, (anim_id_t)((sm->id + 1) % ANIM_COUNT), now_ms);
    anim_update(sm, eyes, now_ms);
}

void anim_prev(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    anim_enter(sm, eyes, (anim_id_t)((sm->id + ANIM_COUNT - 1) % ANIM_COUNT), now_ms);
    anim_update(sm, eyes, now_ms);
}

void anim_set(anim_sm_t *sm, eyes_t *eyes, anim_id_t id, uint32_t now_ms)
{
    anim_enter(sm, eyes, id, now_ms);
    anim_update(sm, eyes, now_ms);
}

void anim_set_audio(anim_sm_t *sm, const audio_features_t *f)
{
    sm->audio = *f;
}

/* sin of an angle in degrees, Q16 */
static int32_t sin_deg_q16(int32_t deg)
{
    deg %= 360;
    if (deg < 0) deg += 360;
    return (int32_t)(sinf((float)deg * 0.01745329f) * 65536.f);
}

static void apply_modulators(anim_sm_t *sm, eyes_t *eyes, const anim_def_t *d, uint32_t now_ms)
{
    const uint32_t el = now_ms - sm->t_enter_ms;
    for (int m = 0; m < 3; m++) {
        const anim_mod_t *md = &d->mods[m];
        if (md->kind == MOD_NONE || md->period_ms == 0) continue;
        for (int e = 0; e < 2; e++) {
            if (!(md->eyes & (1 << e))) continue;
            int32_t delta = 0;
            if (md->kind == MOD_SINE) {
                const int32_t phase = e ? md->phase_r : md->phase_l;
                const int32_t deg = (int32_t)((el % md->period_ms) * 360u / md->period_ms) + phase;
                delta = (int32_t)(((int64_t)md->amp * sin_deg_q16(deg)) >> 16);
            } else {
                /* JITTER: new random target every period, eased toward it */
                if (now_ms - sm->jit_t0_ms >= md->period_ms) {
                    /* one retarget for every jitter modulator sharing the clock */
                    for (int mm = 0; mm < 3; mm++) {
                        if (d->mods[mm].kind != MOD_JITTER) continue;
                        for (int ee = 0; ee < 2; ee++) {
                            const int f = d->mods[mm].field;
                            sm->jit_from[ee][f] = sm->jit_to[ee][f];
                            const int32_t r = (int32_t)(rng_next(sm) % 20001) - 10000;   /* -1..1 in 1e-4 */
                            sm->jit_to[ee][f] = (int32_t)(((int64_t)d->mods[mm].amp * r) / 10000);
                        }
                    }
                    sm->jit_t0_ms = now_ms;
                }
                const int32_t k = ease_in_out_q16((int32_t)(((uint64_t)(now_ms - sm->jit_t0_ms) << 16) / md->period_ms));
                const int f = md->field;
                delta = sm->jit_from[e][f] + (int32_t)(((int64_t)(sm->jit_to[e][f] - sm->jit_from[e][f]) * k) >> 16);
            }
            eyes->mod[e].v[md->field] += delta;
        }
    }
}

/*
 * Dance: the eyes follow the music.
 *   bass level      -> the eyes grow and squash (breathing with the bass)
 *   beat onset      -> a jump, alternating a lean to the left and right
 *   loudness        -> happy arcs on the bottom lids
 *   stereo balance  -> the gaze turns toward the louder side
 *   silence         -> back to neutral, then heavy lids after a while
 */
static void apply_dance(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    const audio_features_t *a = &sm->audio;
    const float alpha = 0.25f;     /* per-frame smoothing at 60 fps */
    sm->dance_bass += (a->bass - sm->dance_bass) * alpha;
    sm->dance_loud += (a->loud - sm->dance_loud) * alpha;
    sm->dance_bal += (a->balance - sm->dance_bal) * 0.1f;

    if (a->beat_count != sm->dance_beats_seen) {
        sm->dance_beats_seen = a->beat_count;
        sm->dance_beat_ms = now_ms;
        sm->dance_side = -sm->dance_side;
    }
    if (a->loud > 0.08f) sm->dance_last_sound_ms = now_ms;
    const uint32_t quiet_ms = now_ms - sm->dance_last_sound_ms;

    /* beat envelope: 1 at the hit, gone after 380 ms */
    float env = 0.f;
    if (sm->dance_beat_ms) {
        const uint32_t since = now_ms - sm->dance_beat_ms;
        if (since < 380) {
            const float t = (float)since / 380.f;
            env = (1.f - t) * (1.f - t);
        }
    }
    /* fade the whole choreography out when the room goes quiet */
    float music = 1.f;
    if (quiet_ms > 1500) {
        music = 1.f - (float)(quiet_ms - 1500) / 2500.f;
        if (music < 0.f) music = 0.f;
    }

    const float bass = sm->dance_bass * music;
    const float loud = sm->dance_loud * music;
    const float kick = env * music;
    const float side = (float)sm->dance_side;

    for (int e = 0; e < 2; e++) {
        eye_pose_t *m = &eyes->mod[e];
        m->sy = (int32_t)((0.28f * bass + 0.10f * kick) * 65536.f);
        m->sx = (int32_t)((0.10f * bass - 0.08f * kick) * 65536.f);
        m->dy = (int32_t)((-16.f * kick) * 65536.f);
        m->dx = (int32_t)((7.f * kick * side + 9.f * sm->dance_bal * music) * 65536.f);
        m->slant = (int32_t)((0.22f * kick * side) * 65536.f);
        m->angle = (int32_t)((9.f * kick * side) * 65536.f);
        m->curve = (int32_t)((0.55f * loud) * 65536.f);
        m->lid_bottom = (int32_t)((0.05f * loud) * 65536.f);
        /* quiet for a long time: lids sag */
        if (quiet_ms > 8000) {
            float sag = (float)(quiet_ms - 8000) / 6000.f;
            if (sag > 1.f) sag = 1.f;
            m->lid_top = (int32_t)(0.45f * sag * 65536.f);
            m->dy += (int32_t)(4.f * sag * 65536.f);
        }
    }
    /* blink a little more in a lively room */
    eyes_set_idle_rates(eyes, (int32_t)((1.0f - 0.4f * loud) * 65536.f), Q16_ONE, Q16(0.5));
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
        eyes_set_target_ex(eyes, 0, &k->left, k->ease_ms, now_ms, (eye_ease_t)k->ease);
        eyes_set_target_ex(eyes, 1, &k->right, k->ease_ms, now_ms, (eye_ease_t)k->ease);
        sm->next_kf++;
    }

    eyes_clear_mod(eyes);
    if (sm->id == ANIM_DANCE) {
        apply_dance(sm, eyes, now_ms);
    } else {
        apply_modulators(sm, eyes, d, now_ms);
    }
}
