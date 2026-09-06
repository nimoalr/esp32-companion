#include "anim.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
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
    eye_tint_t tint[2];             /* colour tint of the expression, per eye */
    anim_mod_t mods[3];
} anim_def_t;

/* pose fields, then the face-level fields a modulator may target */
enum { F_SX, F_SY, F_LT, F_LB, F_SL, F_CV, F_DX, F_DY, F_ANG, F_BEND, F_RTL, F_RTR, F_RBL, F_RBR,
       F_YTL, F_YTR, F_YBL, F_YBR, F_SLB };
enum { F_FACE_SCALE = 100, F_FACE_DX, F_FACE_DY };
#define BOTH 3
#define LEFT 1
#define RIGHT 2

/*
 * pose: sx, sy, lid_top, lid_bottom, slant, curve, dx, dy, then (PX only) the
 * eye angle in degrees, the top lid bend and the four corner radius scales
 * TL, TR, BL, BR (floats -> Q16 at compile time)
 */
#define PXE(sx, sy, lt, lb, sl, cv, dx, dy, ang, bend, rtl, rtr, rbl, rbr, ytl, ytr, ybl, ybr, slb) \
    { { Q16(sx), Q16(sy), Q16(lt), Q16(lb), Q16(sl), Q16(cv), Q16(dx), Q16(dy), Q16(ang), Q16(bend), \
        { Q16(rtl), Q16(rtr), Q16(rbl), Q16(rbr) }, { Q16(ytl), Q16(ytr), Q16(ybl), Q16(ybr) }, Q16(slb) } }
/* PX: circular corners (y radius = x radius), straight bottom lid */
#define PX(sx, sy, lt, lb, sl, cv, dx, dy, ang, bend, rtl, rtr, rbl, rbr) \
    PXE(sx, sy, lt, lb, sl, cv, dx, dy, ang, bend, rtl, rtr, rbl, rbr, rtl, rtr, rbl, rbr, 0)
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
#define SAD_L(dy) PXE(1.00, 0.95, 0.12, 0.10, -0.30, -0.08, 0, dy, -9, 0.10, 1.2, 1.2, 1.0, 1.0, 1.2, 1.2, 1.3, 1.3, 0.12)
#define SAD_R(dy) PXE(1.00, 0.95, 0.12, 0.10, 0.30, -0.08, 0, dy, 9, 0.10, 1.2, 1.2, 1.0, 1.0, 1.2, 1.2, 1.3, 1.3, -0.12)
static const anim_kf_t kf_sad[] = {
    { 0,   250, SAD_L(2),  SAD_R(2), 0 },
    { 250, 900, SAD_L(10), SAD_R(10), 0 },
};
static const anim_kf_t kf_angry[] = {
    { 0, 220, PXE(1.00, 0.74, 0.18, 0.06, 0.34, 0.00, 2, 0, 7, -0.06, 1.0, 0.25, 1.0, 0.7, 1.0, 0.5, 1.0, 0.7, -0.10),
              PXE(1.00, 0.74, 0.18, 0.06, -0.34, 0.00, -2, 0, -7, -0.06, 0.25, 1.0, 0.7, 1.0, 0.5, 1.0, 0.7, 1.0, 0.10), SNAP },
};
#define SURPRISED_POSE PX(1.30, 1.30, 0.00, 0.00, 0.00, 0.00, 0, -2, 0, 0, 1.3, 1.3, 1.3, 1.3)
static const anim_kf_t kf_surprised[] = {
    { 0,  60,  P(0.96, 0.92, 0.00, 0.00, 0.00, 0.00, 0, 2), P(0.96, 0.92, 0.00, 0.00, 0.00, 0.00, 0, 2), 0 },
    { 60, 150, SURPRISED_POSE, SURPRISED_POSE, SNAP },
};
#define SLEEPY(dy) PXE(1.00, 1.00, 0.50, 0.15, 0.00, 0.00, 0, dy, 0, 0.16, 1, 1, 1.15, 1.15, 1, 1, 0.6, 0.6, 0)
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
    { 0, 280, PXE(1.10, 1.16, 0.00, 0.00, 0.00, 0.00, -3, -8, -6, 0, 1.15, 1.15, 1.15, 1.15, 1.4, 1.4, 1.4, 1.4, 0),
              PX(0.98, 0.70, 0.26, 0.04, -0.30, 0.00, -3, 4, 0, 0.08, 1, 1, 0.8, 0.8), SNAP },
};
/* Lids at odds, gaze drifting side to side. */
static const anim_kf_t kf_confused[] = {
    { 0, 280, PX(1.00, 1.00, 0.16, 0.00, 0.32, 0.00, 0, 0, 0, 0.06, 1, 1, 1, 1),
              PX(1.00, 1.05, 0.00, 0.00, 0.00, 0.00, 0, 0, 12, 0, 1.15, 1.15, 1.15, 1.15), 0 },
};
/* Tall, soft eyes with a heartbeat. */
#define LOVE_POSE PXE(1.05, 1.15, 0.00, 0.00, 0.00, 0.12, 0, -2, 0, -0.05, 1.15, 1.15, 1.15, 1.15, 1.45, 1.45, 1.45, 1.45, 0)
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
#define SHY(dy) PXE(1.00, 0.86, 0.14, 0.08, 0.00, -0.06, 6, dy, 0, 0, 1, 1, 1, 1, 1, 1, 1.2, 1.2, 0)
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
/* Squint (Vector's squint primitive): flattened, a touch wider, lids pressed down at the outer corners. */
#define SQUINT_POSE PX(1.05, 0.40, 0.10, 0.06, 0.00, 0.00, 0, 2, 0, 0.10, 0.6, 0.6, 0.6, 0.6)
static const anim_kf_t kf_squint[] = {
    { 0, 250, SQUINT_POSE, SQUINT_POSE, 0 },
};
/* Dance: the pose is neutral, everything happens in the modulation layer. */
static const anim_kf_t kf_dance[] = {
    { 0, 250, NEUTRAL_POSE, NEUTRAL_POSE, 0 },
};

/* ---- character performances: glance, thought, reaction, then a readable hold ---- */
#define SMUG_L(dx) PX(1.06, 0.88, 0.22, 0.00, -0.08, 0.28, dx, -3, -4, 0.02, 1.1, 1.1, 1, 1)
#define SMUG_R(dx) PX(1.00, 0.82, 0.40, 0.00, 0.04, 0.12, dx, 1, 3, 0.02, 1, 1, 1, 1)
static const anim_kf_t kf_smug[] = {
    { 0, 340, SMUG_L(8), SMUG_R(8), 0 },
    { 1900, 420, SMUG_L(0), SMUG_R(0), 0 },
};

/* Narrow together, check one side, then the other: distinct from a raised eyebrow. */
#define SUSP(dx) PX(0.98, 0.76, 0.36, 0.08, 0, 0.04, dx, 2, 0, 0.02, 0.8, 0.8, 1, 1)
static const anim_kf_t kf_suspicious[] = {
    { 0, 300, SUSP(0), SUSP(0), 0 },
    { 450, 450, SUSP(-17), SUSP(-17), 0 },
    { 2100, 560, SUSP(17), SUSP(17), 0 },
    { 4100, 420, SUSP(0), SUSP(0), 0 },
};

/* A little brace before the focused stare; no angry red tint. */
#define FOCUS_L PX(1.03, 0.78, 0.12, 0.04, 0.23, 0.04, 4, -3, 3, -0.02, 1, 0.5, 1, 0.8)
#define FOCUS_R PX(1.03, 0.78, 0.12, 0.04, -0.23, 0.04, -4, -3, -3, -0.02, 0.5, 1, 0.8, 1)
static const anim_kf_t kf_determined[] = {
    { 0, 110, SQUINT_POSE, SQUINT_POSE, 0 },
    { 160, 240, FOCUS_L, FOCUS_R, SNAP },
};

/* Round, close-set eyes looking up; the inner lid corners lift gently. */
#define PLEASE_L(dy) PXE(1.03, 1.18, 0.04, 0.02, -0.18, -0.03, 8, dy, -4, -0.02, 1.35, 1.35, 1.2, 1.2, 1.5, 1.5, 1.4, 1.4, 0)
#define PLEASE_R(dy) PXE(1.03, 1.18, 0.04, 0.02, 0.18, -0.03, -8, dy, 4, -0.02, 1.35, 1.35, 1.2, 1.2, 1.5, 1.5, 1.4, 1.4, 0)
static const anim_kf_t kf_pleading[] = {
    { 0, 450, PLEASE_L(-7), PLEASE_R(-7), 0 },
    { 1700, 600, PLEASE_L(-12), PLEASE_R(-12), 0 },
};

/* Check whether anyone is watching, grin, then wink. Long quiet hold afterward. */
#define IMP(dx) PX(1.04, 0.88, 0.16, 0.00, 0, 0.34, dx, 0, 0, 0, 1.15, 1.15, 0.8, 0.8)
static const anim_kf_t kf_mischievous[] = {
    { 0, 260, IMP(-12), IMP(-12), 0 },
    { 600, 220, IMP(12), IMP(12), SNAP },
    { 1100, 260, IMP(0), IMP(0), 0 },
    { 1580, 110, IMP(0), CLOSED_POSE, 0 },
    { 1860, 240, IMP(0), IMP(0), 0 },
};

/* Caught looking: recoil, glance down, one eye dares a quick peek back. */
#define EMB_L PX(0.98, 0.76, 0.14, 0.04, -0.08, 0, -13, 15, -6, 0.02, 1, 1, 1.2, 1.2)
#define EMB_R PX(0.98, 0.70, 0.20, 0.04, 0.08, 0, -13, 17, 5, 0.02, 1, 1, 1.2, 1.2)
static const anim_kf_t kf_embarrassed[] = {
    { 0, 150, P(0.95, 1.08, 0, 0, 0, 0, 0, -4), P(0.95, 1.08, 0, 0, 0, 0, 0, -4), SNAP },
    { 320, 420, EMB_L, EMB_R, 0 },
    { 1950, 240, EMB_L, PX(1.02, 0.96, 0.06, 0, 0, 0, -4, 3, 1, 0, 1.2, 1.2, 1.2, 1.2), 0 },
    { 2600, 360, EMB_L, EMB_R, 0 },
};

/* An exhale: close, sink slightly, reopen into a soft smile. */
#define RELAXED PX(1.04, 0.86, 0.04, 0.02, 0, 0.26, 0, 5, 0, 0, 1.2, 1.2, 1, 1)
static const anim_kf_t kf_relieved[] = {
    { 0, 200, P(1.08, 0.05, 0, 0, 0, 0, 0, 5), P(1.08, 0.05, 0, 0, 0, 0, 0, 5), 0 },
    { 460, 650, RELAXED, RELAXED, 0 },
};

/* Notice -> dismiss -> wait, WHAT? -> inspect. */
static const anim_kf_t kf_double_take[] = {
    { 0, 200, LOOK(-12, 0), LOOK(-12, 0), 0 },
    { 430, 170, NEUTRAL_POSE, NEUTRAL_POSE, 0 },
    { 680, 150, PX(1.17, 1.26, 0, 0, 0, 0, -18, -8, -5, 0, 1.25, 1.25, 1.25, 1.25),
                PX(1.08, 1.15, 0, 0, 0, 0, -18, -5, -2, 0, 1.2, 1.2, 1.2, 1.2), SNAP },
    { 1500, 440, PX(1.08, 1.10, 0, 0, 0, 0, -10, -4, -5, 0, 1.2, 1.2, 1.2, 1.2),
                 PX(1.00, 0.82, 0.20, 0.02, -0.15, 0, -10, 4, 0, 0.04, 1, 1, 1, 1), 0 },
};

/* Over-shaken: the actual eyes crumple, rather than being replaced by X glyphs.
 * Round narrow capsules, unequal height/tilt, and only a 1.5 px settling breath. */
#define KO_L PX(1.08, 0.14, 0, 0, 0, 0, -2, 12, 12, 0, 1.6, 1.6, 1.6, 1.6)
#define KO_R PX(1.02, 0.10, 0, 0, 0, 0, 2, 17, -9, 0, 1.6, 1.6, 1.6, 1.6)
static const anim_kf_t kf_knocked_out[] = {
    { 0, 120, P(1.04, 0.58, 0.18, 0.08, 0, 0, 0, 4), P(1.00, 0.72, 0.20, 0.04, 0, 0, 0, 2), 0 },
    { 170, 410, KO_L, KO_R, 0 },
};

/* Three-second recovery: left peeks, right catches up, blink, find the horizon. */
#define GROG_L PX(1.00, 0.86, 0.32, 0.04, -0.10, 0, 0, 6, -3, 0.05, 1, 1, 1.2, 1.2)
#define GROG_R PX(1.00, 0.80, 0.38, 0.04, 0.10, 0, 0, 9, 3, 0.05, 1, 1, 1.2, 1.2)
static const anim_kf_t kf_recovering[] = {
    { 0, 200, KO_L, KO_R, 0 },
    { 260, 500, GROG_L, KO_R, 0 },
    { 900, 500, GROG_L, GROG_R, 0 },
    { 1600, 130, CLOSED_POSE, CLOSED_POSE, 0 },
    { 1820, 480, P(1, 1, 0.14, 0, 0, 0, 0, 3), P(1, 1, 0.18, 0, 0, 0, 0, 4), 0 },
    { 2450, 500, NEUTRAL_POSE, NEUTRAL_POSE, 0 },
};

/* Playful actions, not emotional resting poses. Silhouettes use the same eye
 * placement/colour; their entrances and exits close through a sliver. */
#define SYMBOL_POSE P(1.02, 0.84, 0, 0, 0, 0, 0, 0)
static const anim_kf_t kf_symbols[] = {{0, 280, SYMBOL_POSE, SYMBOL_POSE, 0}};
static const anim_kf_t kf_nod[] = {
    {0, 160, NEUTRAL_POSE, NEUTRAL_POSE, 0},
    {300, 210, P(1.06,0.82,0,0,0,0,0,14), P(1.06,0.82,0,0,0,0,0,14), 0},
    {590, 200, P(0.98,1.08,0,0,0,0,0,-8), P(0.98,1.08,0,0,0,0,0,-8), SNAP},
    {900, 190, P(1.04,0.92,0,0,0,0,0,9), P(1.04,0.92,0,0,0,0,0,9), 0},
    {1250, 350, NEUTRAL_POSE, NEUTRAL_POSE, 0},
};
static const anim_kf_t kf_peekaboo[] = {
    {0, 220, CLOSED_POSE, CLOSED_POSE, 0},
    {700, 400, P(0.94,0.76,0.12,0,0,0,-8,2), CLOSED_POSE, 0},
    {1350, 150, EXCITED_POSE, EXCITED_POSE, SNAP},
    {2000, 320, HAPPY_POSE, HAPPY_POSE, 0},
    {3200, 480, NEUTRAL_POSE, NEUTRAL_POSE, 0},
};
static const anim_kf_t kf_loading[] = {
    {0, 180, CLOSED_POSE, CLOSED_POSE, 0},
    {260, 300, P(0.52,0.15,0,0,0,0,0,0), P(0.52,0.15,0,0,0,0,0,0), 0},
    {3800, 180, CLOSED_POSE, CLOSED_POSE, 0},
    {4100, 380, NEUTRAL_POSE, NEUTRAL_POSE, SNAP},
};
static const anim_kf_t kf_boop[] = {
    {0, 200, NEUTRAL_POSE, NEUTRAL_POSE, 0},
    {450, 120, P(1.24,0.40,0,0,0,0,0,12), P(1.24,0.40,0,0,0,0,0,12), SNAP},
    {650, 180, P(0.92,1.25,0,0,0,0,0,-7), P(0.92,1.25,0,0,0,0,0,-7), SNAP},
    {940, 280, P(1.08,0.90,0,0,0,0,0,4), P(1.08,0.90,0,0,0,0,0,4), 0},
    {1400, 400, NEUTRAL_POSE, NEUTRAL_POSE, 0},
};
static const anim_kf_t kf_sneeze[] = {
    {0, 420, P(0.95,0.92,0.24,0,0,0,0,-5), P(0.95,0.92,0.24,0,0,0,0,-5), 0},
    {650, 350, P(0.91,1.18,0.12,0,0,0,0,-12), P(0.91,1.18,0.12,0,0,0,0,-12), 0},
    {1180, 100, P(1.25,0.045,0,0,0,0,0,16), P(1.25,0.045,0,0,0,0,0,16), SNAP},
    {1420, 360, SQUINT_POSE, SQUINT_POSE, 0},
    {2100, 600, NEUTRAL_POSE, NEUTRAL_POSE, 0},
};

/*
 * tint: hue shift (deg), blend toward a colour (0xRRGGBB, weight 0..1), saturation and lightness
 * multipliers. The base hue stays the character's identity; expressions only lean on it.
 */
#define TT(shift, prgb, pw, sat, lum) { Q16(shift), Q16(pw), Q16(sat), Q16(lum), prgb }
#define T(shift, prgb, pw, sat, lum)  { TT(shift, prgb, pw, sat, lum), TT(shift, prgb, pw, sat, lum) }
#define T2(l, r)                      { l, r }
#define TNONE                         T(0, 0, 0, 1.0, 1.0)
#define RED   0xFF3020
#define PINK  0xFF5AAA
#define GREEN 0x78C83C

/* name, keyframes, loop ms, blink interval scale, blink speed scale, dart amplitude scale, tint, modulators */
#define DEF(nm, arr, loop, bi, bs, ds, tint, ...) \
    { nm, arr, (int)(sizeof(arr) / sizeof(arr[0])), loop, Q16(bi), Q16(bs), Q16(ds), tint, { __VA_ARGS__ } }

static const anim_def_t k_anims[ANIM_COUNT] = {
    [ANIM_NEUTRAL]     = DEF("NEUTRAL",     kf_neutral,     0,    1.0, 1.0, 1.0, TNONE, NOMOD),
    [ANIM_HAPPY]       = DEF("HAPPY",       kf_happy,       0,    1.0, 1.0, 0.9, T(4, 0, 0, 1.05, 1.08), NOMOD),
    [ANIM_SAD]         = DEF("SAD",         kf_sad,         0,    1.3, 1.3, 0.5, T(0, 0, 0, 0.70, 0.78), NOMOD),
    [ANIM_ANGRY]       = DEF("ANGRY",       kf_angry,       0,    1.0, 1.0, 0.6, T(0, RED, 0.30, 1.15, 1.05), NOMOD),
    [ANIM_SURPRISED]   = DEF("SURPRISED",   kf_surprised,   0,    1.6, 1.0, 0.3, T(0, 0, 0, 1.00, 1.15), NOMOD),
    [ANIM_SLEEPY]      = DEF("SLEEPY",      kf_sleepy,      5300, 2.5, 2.2, 0.4, T(0, 0, 0, 0.85, 0.70), NOMOD),
    [ANIM_LOOK_AROUND] = DEF("LOOK_AROUND", kf_look_around, 3600, 1.0, 1.0, 0.5, TNONE, NOMOD),
    [ANIM_WINK]        = DEF("WINK",        kf_wink,        3500, 1.0, 1.0, 0.8, T2(TT(0, 0, 0, 1.00, 1.04), TT(0, 0, 0, 0.95, 0.90)), NOMOD),
    [ANIM_CURIOUS]     = DEF("CURIOUS",     kf_curious,     0,    1.2, 1.0, 0.7, T2(TT(0, 0, 0, 1.00, 1.10), TT(0, 0, 0, 0.92, 0.92)),
                             SINE(F_DY, LEFT, 3.0, 1500, 0, 0)),
    [ANIM_CONFUSED]    = DEF("CONFUSED",    kf_confused,    0,    1.0, 1.0, 0.8, T(0, 0, 0, 0.90, 0.98),
                             SINE(F_DX, BOTH, 8.0, 1600, 0, 0), SINE(F_DY, RIGHT, 4.0, 900, 0, 90),
                             SINE(F_ANG, RIGHT, 4.0, 1600, 90, 90)),
    [ANIM_LOVE]        = DEF("LOVE",        kf_love,        0,    1.4, 1.2, 0.5, T(0, PINK, 0.35, 1.05, 1.08),
                             SINE(F_SY, BOTH, 0.07, 700, 0, 0), SINE(F_SX, BOTH, 0.05, 700, 0, 0)),
    [ANIM_DIZZY]       = DEF("DIZZY",       kf_dizzy,       0,    2.0, 1.5, 0.0, T(0, GREEN, 0.30, 0.90, 0.90),
                             SINE(F_DX, BOTH, 10.0, 900, 0, 180), SINE(F_DY, BOTH, 10.0, 900, 90, 270),
                             SINE(F_ANG, BOTH, 14.0, 900, 180, 0)),
    [ANIM_LAUGHING]    = DEF("LAUGHING",    kf_laughing,    0,    1.5, 1.0, 0.5, T(4, 0, 0, 1.05, 1.10),
                             SINE(F_DY, BOTH, 5.0, 260, 0, 0), SINE(F_SY, BOTH, 0.05, 260, 90, 90)),
    [ANIM_SCARED]      = DEF("SCARED",      kf_scared,      0,    3.0, 0.6, 1.4, T(0, 0, 0, 0.55, 1.12),
                             JITTER(F_DX, BOTH, 3.0, 50), JITTER(F_DY, BOTH, 3.0, 50)),
    [ANIM_SKEPTICAL]   = DEF("SKEPTICAL",   kf_skeptical,   0,    1.0, 1.0, 0.6, T2(TT(0, 0, 0, 0.85, 0.92), TT(0, 0, 0, 1.00, 1.06)), NOMOD),
    [ANIM_THINKING]    = DEF("THINKING",    kf_thinking,    5200, 1.3, 1.3, 0.6, T(0, 0, 0, 0.95, 0.92),
                             SINE(F_DX, BOTH, 2.0, 2500, 0, 0)),
    [ANIM_BORED]       = DEF("BORED",       kf_bored,       6000, 1.6, 1.8, 0.5, T(0, 0, 0, 0.85, 0.85), NOMOD),
    [ANIM_EXCITED]     = DEF("EXCITED",     kf_excited,     0,    0.7, 0.8, 1.2, T(0, 0, 0, 1.10, 1.12),
                             SINE(F_FACE_DY, LEFT, 8.0, 320, 0, 0), SINE(F_FACE_SCALE, LEFT, 0.03, 320, 90, 0)),
    [ANIM_SHY]         = DEF("SHY",         kf_shy,         5000, 1.0, 1.0, 0.6, T(0, PINK, 0.20, 1.00, 0.95), NOMOD),
    [ANIM_ANNOYED]     = DEF("ANNOYED",     kf_annoyed,     4500, 1.0, 1.0, 0.5, T(0, RED, 0.12, 1.00, 0.90), NOMOD),
    [ANIM_SLEEPING]    = DEF("SLEEPING",    kf_sleeping,    0,    20.0, 3.0, 0.0, T(0, 0, 0, 0.80, 0.45),
                             SINE(F_DY, BOTH, 2.0, 3200, 0, 0), SINE(F_FACE_SCALE, LEFT, 0.015, 3200, 270, 0)),
    [ANIM_SQUINT]      = DEF("SQUINT",      kf_squint,      0,    1.4, 1.2, 0.4, T(0, 0, 0, 0.95, 0.92), NOMOD),
    [ANIM_DANCE]       = DEF("DANCE",       kf_dance,       0,    1.0, 1.0, 0.5, TNONE, NOMOD),
    [ANIM_SMUG]        = DEF("SMUG", kf_smug, 4800, 1.4, 1.2, 0.2, TNONE, NOMOD),
    [ANIM_SUSPICIOUS]  = DEF("SUSPICIOUS", kf_suspicious, 5700, 1.8, 1.1, 0.0, TNONE, NOMOD),
    [ANIM_DETERMINED]  = DEF("DETERMINED", kf_determined, 0, 1.6, 0.8, 0.2, T(0, 0, 0, 1.05, 1.08), NOMOD),
    [ANIM_PLEADING]    = DEF("PLEADING", kf_pleading, 0, 1.4, 1.4, 0.2, T(0, 0, 0, 0.93, 1.08),
                            SINE(F_DY, BOTH, 1.5, 2400, 0, 0)),
    [ANIM_MISCHIEVOUS] = DEF("MISCHIEVOUS", kf_mischievous, 5400, 1.5, 1.0, 0.15, T(0, 0, 0, 1, 1.06), NOMOD),
    [ANIM_EMBARRASSED] = DEF("EMBARRASSED", kf_embarrassed, 5400, 1.3, 1.2, 0.15, T(0, PINK, 0.18, 0.95, 0.96), NOMOD),
    [ANIM_RELIEVED]    = DEF("RELIEVED", kf_relieved, 0, 1.7, 1.5, 0.35, T(0, 0, 0, 0.95, 1.02), NOMOD),
    [ANIM_DOUBLE_TAKE] = DEF("DOUBLE_TAKE", kf_double_take, 5200, 1.4, 0.9, 0.0, TNONE, NOMOD),
    [ANIM_KNOCKED_OUT] = DEF("KNOCKED_OUT", kf_knocked_out, 0, 0.0, 1.0, 0.0, T(0, 0, 0, 0.80, 0.78),
                            SINE(F_DY, BOTH, 1.5, 2800, 0, 0)),
    [ANIM_RECOVERING]  = DEF("RECOVERING", kf_recovering, 0, 0.0, 1.0, 0.0, T(0, 0, 0, 0.93, 0.96), NOMOD),
    [ANIM_HEARTS]      = DEF("HEARTS", kf_symbols, 0, 1.8, 1.0, 0.1, T(0, PINK, 0.45, 1, 1.06), NOMOD),
    [ANIM_HEARTBREAK]  = DEF("HEARTBREAK", kf_symbols, 5600, 0.0, 1.0, 0.0, T(0, PINK, 0.38, 0.9, 0.98), NOMOD),
    [ANIM_HIGH_ROLLER] = DEF("HIGH_ROLLER", kf_symbols, 6200, 0.0, 1.0, 0.0, T(0, 0xFFD040, 0.32, 1, 1.1), NOMOD),
    [ANIM_NOD]         = DEF("NOD", kf_nod, 4700, 1.0, 1.0, 0.15, TNONE, NOMOD),
    [ANIM_PEEKABOO]    = DEF("PEEKABOO", kf_peekaboo, 5200, 0.0, 1.0, 0.0, TNONE, NOMOD),
    [ANIM_LOADING]     = DEF("LOADING", kf_loading, 6200, 0.0, 1.0, 0.0, TNONE, NOMOD),
    [ANIM_BOOP]        = DEF("BOOP", kf_boop, 4500, 1.0, 1.0, 0.1, TNONE, NOMOD),
    [ANIM_SNEEZE]      = DEF("SNEEZE", kf_sneeze, 5300, 0.0, 1.0, 0.0, TNONE, NOMOD),
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
    sm->t_change_ms = now_ms;
    for (int e = 0; e < 2; e++) sm->previous_symbol[e] = eyes->symbol[e];
    sm->previous_split = eyes->symbol_split;
    for (int e = 0; e < 2; e++) sm->previous_reel[e] = eyes->reel_pos[e];
    for (int e = 0; e < 2; e++) sm->previous_gate[e] = eyes->shape_gate[e];
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
    memset(sm->dance_bars, 0, sizeof sm->dance_bars);
    sm->dance_visual = 0;
    sm->dance_visual_last = 0;
    sm->dance_visual_ms = now_ms;
    sm->dance_visual_len = 8000 + rng_next(sm) % 8000;      /* the first show piece comes soon */
    sm->dance_visual_mix = 0.f;
    eyes_set_fx(eyes, 0, 0.f);
    eyes_clear_mod(eyes);
    eyes_set_idle_rates(eyes, d->blink_interval_scale, d->blink_speed_scale, d->dart_scale);
    eyes_set_tint(eyes, d->tint, 450, now_ms);
    ESP_LOGI(TAG, "-> %s", d->name);
}

void anim_init(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    memset(sm, 0, sizeof *sm);
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
            if (md->field >= F_FACE_SCALE) {
                if (e == 0) {   /* face-level fields are applied once */
                    if (md->field == F_FACE_SCALE) eyes->face_mod_scale += delta;
                    else if (md->field == F_FACE_DX) eyes->face_mod_dx += delta;
                    else eyes->face_mod_dy += delta;
                }
            } else {
                eyes->mod[e].v[md->field] += delta;
            }
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
void anim_dance_flourish(anim_sm_t *sm, int kind, uint32_t now_ms)
{
    sm->dance_flourish = kind;
    sm->dance_flourish_ms = now_ms;
}

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
    /* an uncertain rhythm moves him less than a locked one */
    const float kick = env * music * (0.45f + 0.55f * a->regularity);
    const float side = (float)sm->dance_side;

    /* a flourish: a move layered on the beat for a moment, from a poke or a stroke */
    float fl_shimmy = 0.f;
    if (sm->dance_flourish) {
        /* a stroke: a slow sway for a couple of seconds, easing in and out */
        const float t = (float)(now_ms - sm->dance_flourish_ms) / 1000.f;
        const float env_in = t < 0.3f ? t / 0.3f : 1.f;
        const float fade = t < 2.2f ? env_in * (t < 1.4f ? 1.f : 1.f - (t - 1.4f) / 0.8f) : 0.f;
        if (fade <= 0.f) sm->dance_flourish = 0;
        else fl_shimmy = fade * sinf(t * 8.f);
    }

    for (int e = 0; e < 2; e++) {
        eye_pose_t *m = &eyes->mod[e];
        m->sy = (int32_t)((0.28f * bass + 0.10f * kick) * 65536.f);
        m->sx = (int32_t)((0.10f * bass - 0.08f * kick) * 65536.f);
        m->dy = (int32_t)((-16.f * kick) * 65536.f);
        m->dx = (int32_t)((7.f * kick * side + 9.f * sm->dance_bal * music + 12.f * fl_shimmy) * 65536.f);
        m->slant = (int32_t)((0.22f * kick * side + 0.18f * fl_shimmy) * 65536.f);
        m->angle = (int32_t)((9.f * kick * side + 8.f * fl_shimmy) * 65536.f);
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
    /*
     * Passing visuals: the plain dance most of the time, and now and then one of the show
     * pieces for a while (the spectrum eyes today, the disco ball next), switched on a beat,
     * fading in and out. Each stays 10-20 s, the plain dance 15-35 s between them.
     */
    const bool beat_now = sm->dance_beat_ms && now_ms - sm->dance_beat_ms < 40;
    const uint32_t shown_for = now_ms - sm->dance_visual_ms;
    if (shown_for >= sm->dance_visual_len && (beat_now || shown_for >= sm->dance_visual_len + 1500)) {
        if (sm->dance_visual) {
            sm->dance_visual_last = sm->dance_visual;
            sm->dance_visual = 0;
            sm->dance_visual_len = 15000 + rng_next(sm) % 20000;
        } else {
            /* one of the show pieces, never the one that just went */
            int pick = 1 + (int)(rng_next(sm) % 3u);
            if (pick == sm->dance_visual_last) pick = 1 + pick % 3;
            sm->dance_visual = pick;
            sm->dance_visual_len = 10000 + rng_next(sm) % 10000;
        }
        sm->dance_visual_ms = now_ms;
    }
    const float mix_want = sm->dance_visual ? 1.f : 0.f;
    sm->dance_visual_mix += (mix_want - sm->dance_visual_mix) * 0.06f;
    if (sm->dance_visual_mix < 0.01f) sm->dance_visual_mix = 0.f;
    const int fx_shown = sm->dance_visual ? sm->dance_visual : sm->dance_visual_last;
    /* spectrum eyes: the same eight bands in both eyes (pairs of the sixteen), rising from the
     * bottom of each eye; they jump up with the sound and fall back at their own pace */
    for (int i = 0; i < 8; i++) {
        const float b0 = a->bands[2 * i], b1 = a->bands[2 * i + 1];
        const float want = (b0 > b1 ? b0 : b1) * music;
        float *h = &sm->dance_bars[0][i];
        if (want > *h) *h += (want - *h) * 0.6f;
        else *h -= 0.045f;
        if (*h < 0.f) *h = 0.f;
        sm->dance_bars[1][i] = *h;
    }
    eyes_set_bar_heights(eyes, 0, sm->dance_bars[0]);
    eyes_set_bar_heights(eyes, 1, sm->dance_bars[1]);
    /* the mirror ball: a slow turn that quickens with the sound, facets flashing in time */
    sm->disco_spin += (0.0009f + 0.004f * loud) * 1.f;
    if (sm->disco_spin > 1000.f) sm->disco_spin -= 1000.f;
    eyes_set_disco(eyes, sm->disco_spin, (uint32_t)(now_ms / 120) + (beat_now ? 7u : 0u));
    /* the spotlights: three beams wandering on slow curves, each thrown to a new spot on the beat */
    {
        const float t = (float)now_ms * 0.001f;
        float sx[3], sy[3];
        const float kickf = kick;
        sx[0] = 0.6f * sinf(t * 1.3f) + 0.25f * side * kickf;
        sy[0] = 0.5f * cosf(t * 0.9f);
        sx[1] = 0.6f * sinf(t * 0.7f + 2.f) - 0.25f * side * kickf;
        sy[1] = 0.5f * sinf(t * 1.1f + 1.f);
        sx[2] = 0.55f * cosf(t * 1.7f + 4.f);
        sy[2] = 0.5f * sinf(t * 0.6f + 3.f) - 0.2f * kickf;
        eyes_set_spots(eyes, 3, sx, sy, 0.34f + 0.12f * kickf);
    }
    eyes_set_fx(eyes, (sm->dance_visual_mix > 0.f && music > 0.02f) ? fx_shown : 0, sm->dance_visual_mix);
    /* the whole face pulses with the bass */
    eyes->face_mod_scale = (int32_t)((0.06f * bass) * 65536.f);
    /* the colour flashes a little brighter on the hit and shimmers with the bass */
    eyes->tint_mod_lum = (int32_t)((0.22f * kick) * 65536.f);
    eyes->tint_mod_hue = (int32_t)((8.f * bass * side) * 65536.f);
    /* blink a little more in a lively room */
    eyes_set_idle_rates(eyes, (int32_t)((1.0f - 0.4f * loud) * 65536.f), Q16_ONE, Q16(0.5));
}

static int32_t ramp(uint32_t t, uint32_t start, uint32_t length)
{
    if (t <= start) return 0;
    if (t-start >= length) return Q16_ONE;
    return ease_in_out_q16((int32_t)((uint64_t)(t-start)*Q16_ONE/length));
}

static void apply_performance(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms)
{
    const uint32_t el = now_ms-sm->t_enter_ms;
    eyes->symbol[0] = eyes->symbol[1] = EYE_SYMBOL_NONE;
    eyes->symbol_split = 0;
    if (sm->id == ANIM_HEARTS || sm->id == ANIM_HEARTBREAK) {
        eyes->symbol[0] = eyes->symbol[1] = EYE_SYMBOL_HEART;
        const uint32_t beat = el % 1000;
        /* Two taps of a heartbeat followed by a rest, not a continuous sine wobble. */
        const float p = beat < 180 ? sinf((float)beat*3.14159265f/180.f)
                       : beat >= 230 && beat < 370 ? 0.55f*sinf((float)(beat-230)*3.14159265f/140.f) : 0.f;
        float pulse = p;
        if (sm->id == ANIM_HEARTBREAK) {
            int32_t opening = ramp(el, 750, 850);
            opening -= (int32_t)((int64_t)opening*ramp(el, 3200, 900) >> 16);
            if (opening) eyes->symbol[0] = eyes->symbol[1] = EYE_SYMBOL_BROKEN;
            eyes->symbol_split = opening/5;
            pulse *= 1.f-(float)opening/Q16_ONE;
            eyes->tint_mod_lum -= opening/8;
        }
        for (int e = 0; e < 2; e++) {
            eyes->mod[e].sx += (int32_t)(0.09f*pulse*Q16_ONE);
            eyes->mod[e].sy += (int32_t)(0.11f*pulse*Q16_ONE);
        }
    } else if (sm->id == ANIM_HIGH_ROLLER) {
        for (int e = 0; e < 2; e++) {
            const uint32_t stop = e ? 3350 : 2400;
            const int turns = e ? 18 : 12;
            eyes->symbol[e] = EYE_SYMBOL_REEL;
            eyes->reel_pos[e] = turns*ramp(el, 400, stop-400);
            if (el >= stop && el < stop+400) {
                const float t = (float)(el-stop)/400.f;
                eyes->reel_pos[e] += (int32_t)(0.065f*sinf(t*6.2831853f)*(1.f-t)*Q16_ONE);
            }
            /* Close across the loop seam before rewinding the reel strip. */
            if (el < 180) eyes->shape_gate[e] = (int32_t)((int64_t)eyes->shape_gate[e]*ramp(el,0,180) >> 16);
            if (el > 6020) eyes->shape_gate[e] = Q16_ONE-ramp(el,6020,180);
        }
        if (el >= 3750 && el < 4250) {
            const float t = (float)(el-3750)/500.f;
            const float bounce = sinf(t*6.2831853f)*(1.f-t);
            eyes->face_mod_dy = (int32_t)(-9.f*bounce*Q16_ONE);
            eyes->face_mod_scale = (int32_t)(0.08f*fabsf(bounce)*Q16_ONE);
        }
    } else if (sm->id == ANIM_LOADING && el >= 500 && el < 3980) {
        const int32_t angle = (int32_t)((uint64_t)((el-500)%900)*360*Q16_ONE/900);
        eyes->mod[0].angle = angle; eyes->mod[1].angle = -angle;
    }
    if (sm->id == ANIM_PEEKABOO || sm->id == ANIM_LOADING || sm->id == ANIM_SNEEZE) {
        const uint32_t end = sm->id == ANIM_PEEKABOO ? 3700 : sm->id == ANIM_LOADING ? 4500 : 2800;
        eyes_set_idle_rates(eyes, el < end ? 0 : Q16_ONE, Q16_ONE, el < end ? 0 : Q16(0.2));
    }

    /* Cross an actual selection change by closing the old silhouette, swapping
     * only while thin, then reopening the new one. Ordinary poses still morph. */
    const uint32_t change = now_ms-sm->t_change_ms;
    if (change < 280) {
        for (int e = 0; e < 2; e++) {
            if (sm->previous_symbol[e] == eyes->symbol[e] && sm->previous_gate[e] == Q16_ONE) continue;
            const int32_t gate = change < 120
                ? (int32_t)((int64_t)sm->previous_gate[e]*(Q16_ONE-ramp(change,0,120)) >> 16)
                : ramp(change,120,160);
            eyes->shape_gate[e] = (int32_t)((int64_t)eyes->shape_gate[e]*gate >> 16);
            if (change < 120) {
                eyes->symbol[e] = sm->previous_symbol[e];
                eyes->symbol_split = sm->previous_split;
                eyes->reel_pos[e] = sm->previous_reel[e];
            }
        }
    }
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
    /* The selectable recovery demo returns to ordinary idle motion after its blink. */
    if (sm->id == ANIM_RECOVERING && el >= 3000) {
        eyes_set_idle_rates(eyes, Q16_ONE, Q16_ONE, Q16(0.5));
    }
    if (sm->id == ANIM_DANCE) {
        apply_dance(sm, eyes, now_ms);
    } else {
        apply_modulators(sm, eyes, d, now_ms);
    }
    apply_performance(sm, eyes, now_ms);
}
