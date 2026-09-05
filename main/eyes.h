/*
 * Eye model: base geometry + an animated pose per eye, eased per field, with
 * an always-on idle layer underneath (blinks, gaze darts) and the secondary
 * motion that makes the eyes read as alive: squash and stretch on darts and
 * fast moves, size changes with gaze direction, lids that thin out and eyes
 * that align when they close. The timing and proportions follow the
 * procedural face of Anki's Cozmo/Vector (see docs/animation.md).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "raster.h"

/* What the renderer draws. All lengths Q16 pixels, fractions Q16 in [0, 1]. */
typedef struct {
    int32_t cx, cy;         /* centre */
    int32_t w, h;           /* full width / height */
    int32_t rad[4];         /* corner x radii TL, TR, BL, BR */
    int32_t rady[4];        /* corner y radii */
    int32_t lid_top;        /* fraction of h hidden from the top, at x == cx */
    int32_t lid_bottom;     /* fraction of h hidden from the bottom, at x == cx */
    int32_t slant;          /* dy/dx of the top lid line (+ descends toward +x) */
    int32_t slant_b;        /* dy/dx of the bottom lid line */
    int32_t bend;           /* top lid bend at the centre, pixels: > 0 droops into the eye, < 0 arches up */
    int32_t curve;          /* bottom lid bend at the centre, pixels: > 0 bulges up into the eye, < 0 sags */
    int32_t hot_x, hot_y;   /* hot spot centre (screen, Q16) */
    int32_t angle;          /* rotation of the eye about its centre, degrees Q16, clockwise on screen */
    uint32_t color;         /* 0xRRGGBB */
} EyeParams;

/* Animated pose, relative to the base geometry. */
#define EYE_POSE_FIELDS 19
typedef union {
    struct {
        int32_t sx, sy;               /* width / height scale, Q16 (ONE = base) */
        int32_t lid_top, lid_bottom;  /* Q16 fractions */
        int32_t slant;                /* Q16 dy/dx */
        int32_t curve;                /* Q16 fraction of the (scaled) height, signed: > 0 happy arc, < 0 the lid sags */
        int32_t dx, dy;               /* Q16 px centre offset */
        int32_t angle;                /* Q16 degrees, clockwise on screen (the eye's right side drops) */
        int32_t bend;                 /* Q16 fraction of the (scaled) height, signed */
        int32_t rad[4];               /* corner x radius scale, Q16 (ONE = base radius): TL, TR, BL, BR */
        int32_t rady[4];              /* corner y radius scale, Q16 */
        int32_t slant_b;              /* Q16 dy/dx of the bottom lid */
    };
    int32_t v[EYE_POSE_FIELDS];
} eye_pose_t;

#define EYE_POSE_NEUTRAL { { Q16_ONE, Q16_ONE, 0, 0, 0, 0, 0, 0, 0, 0, { Q16_ONE, Q16_ONE, Q16_ONE, Q16_ONE }, { Q16_ONE, Q16_ONE, Q16_ONE, Q16_ONE }, 0 } }

/*
 * Colour. The base colour is the character's identity (a user setting); an
 * expression tints it by shifting the hue a little, blending it part of the
 * way toward a colour (anger toward red, love toward pink) and scaling
 * saturation and lightness; mood scales lightness and saturation on top.
 */
typedef struct {
    int32_t hue_shift;      /* Q16 degrees, relative to the base hue */
    int32_t pull;           /* Q16 0..1, how far to blend toward pull_rgb (in RGB, so "toward red" means the same on every base) */
    int32_t sat, lum;       /* Q16 multipliers (ONE = base) */
    uint32_t pull_rgb;      /* 0xRRGGBB */
} eye_tint_t;
#define EYE_TINT_FIELDS 4   /* eased scalars; pull_rgb is cross-faded */
#define EYE_TINT_NEUTRAL { 0, 0, Q16_ONE, Q16_ONE, 0 }

/* Transition easing. */
typedef enum {
    EYE_EASE_SMOOTH = 0,    /* smoothstep on every field */
    EYE_EASE_SNAP,          /* fast start with a small overshoot on size and position (expressive changes) */
} eye_ease_t;

typedef struct {
    EyeParams base;                    /* cx, cy, w, h, rad, color of the resting eye */
    eye_pose_t cur, from, to;
    uint32_t t0_ms[EYE_POSE_FIELDS];
    uint32_t dur_ms[EYE_POSE_FIELDS];
    uint8_t ease;                      /* eye_ease_t of the current transition */
    /* secondary motion state */
    int32_t prev_cx, prev_cy;          /* pose-driven centre last frame, Q16 */
    int32_t motion_k;                  /* smoothed stretch amount, Q16 */
} EyeState;

typedef struct {
    /* blink */
    uint32_t next_blink_ms;
    uint32_t blink_t0_ms;
    bool blinking;
    int32_t blink_interval_scale;      /* Q16; > ONE = rarer blinks */
    int32_t blink_speed_scale;         /* Q16; > ONE = slower close/open */
    /* gaze darts */
    int32_t dart_scale;                /* Q16 amplitude scale; 0 = eyes hold still */
    uint32_t next_dart_ms;
    uint32_t dart_t0_ms;
    uint32_t dart_dur_ms;
    int32_t dart_from_x, dart_from_y, dart_to_x, dart_to_y;   /* Q16 px */
    int32_t dart_squash;               /* Q16 peak vertical squash of this dart */
    bool dart_lag_x;                   /* x lags y (darting down); otherwise y lags x */
    uint32_t rng;
} eyes_idle_t;

typedef struct {
    EyeState eye[2];                   /* 0 = left (viewer's left), 1 = right */
    eye_pose_t mod[2];                 /* per-frame additive modulation (wobble, bounce, beat); deltas */
    eye_pose_t env[2];                 /* environment layer (gravity gaze), set by the behaviour code; deltas */
    eyes_idle_t idle;
    bool face_rot;                     /* whole face rotated about the screen centre */
    float face_deg;
    int32_t face_cos, face_sin;        /* Q16 */
    uint32_t prev_ms;
    bool have_prev;
    /* face-level placement: both eyes shift / scale about the screen centre */
    int32_t face_dx, face_dy;          /* Q16 px, set by the caller */
    int32_t face_scale;                /* Q16, ONE = base */
    int32_t face_mod_dx, face_mod_dy, face_mod_scale;   /* per-frame additive deltas (animation modulators) */
    /* attention (Vector's focus): the eyes settle on a point and stop wandering */
    bool attend;
    int32_t attend_tx, attend_ty;      /* target gaze offset, Q16 px */
    int32_t attend_x, attend_y;        /* eased gaze offset */
    int32_t attend_k;                  /* Q16 0..1, eased on/off */
    /* colour, per eye */
    float base_h, base_s, base_l;      /* HSL of the base colour: degrees, 0..1, 0..1 */
    eye_tint_t tint_cur[2], tint_from[2], tint_to[2];
    uint32_t tint_t0_ms, tint_dur_ms;
    int32_t tint_mod_hue, tint_mod_lum;    /* per-frame additive (Q16 degrees / Q16 lightness multiplier delta) */
    int32_t mood_lum, mood_sat;        /* Q16 multipliers from the behaviour layer */
    uint32_t rgb[2];                   /* colour currently in each LUT, 0xRRGGBB */
    uint16_t lut[2][256];
    /* hot spot (optional): lightness falloff tables and the 32 x 64 lightness x coverage LUTs */
    bool hot;
    uint8_t hot_gx[2][466], hot_gy[2][466];
    uint8_t hot_g2l[RASTER_G2L_N];
    uint16_t lut2[2][32 * 64];
} eyes_t;

void eyes_init(eyes_t *e, uint32_t now_ms);

/* Retarget every pose field of one eye; a retarget during a transition restarts from the current value. */
void eyes_set_target(eyes_t *e, int eye, const eye_pose_t *pose, uint32_t dur_ms, uint32_t now_ms);
void eyes_set_target_ex(eyes_t *e, int eye, const eye_pose_t *pose, uint32_t dur_ms, uint32_t now_ms, eye_ease_t ease);

/* Per-frame additive modulation on top of the eased pose; cleared by eyes_clear_mod(). */
void eyes_clear_mod(eyes_t *e);
void eyes_set_env(eyes_t *e, int eye, const eye_pose_t *delta);

/* Rotate the whole face about the screen centre; degrees, clockwise on screen, 0 = upright. */
void eyes_set_face_angle(eyes_t *e, float deg);

/* Colour: base (identity), expression tint per eye (eased), mood multipliers (applied directly). */
void eyes_set_base_color(eyes_t *e, uint32_t rgb);
void eyes_set_tint(eyes_t *e, const eye_tint_t t[2], uint32_t dur_ms, uint32_t now_ms);
void eyes_set_mood(eyes_t *e, int32_t lum_q16, int32_t sat_q16);
/* The colour one eye is drawn with right now, 0xRRGGBB. */
uint32_t eyes_color(const eyes_t *e, int eye);
/* Hot spot shading on/off (always on in the firmware; the host harnesses compare both). */
void eyes_set_hotspot(eyes_t *e, bool on);

/* Face-level placement, applied directly (callers ease). */
void eyes_set_face_offset(eyes_t *e, int32_t dx_q16, int32_t dy_q16);
void eyes_set_face_scale(eyes_t *e, int32_t scale_q16);

/* Attention: hold the gaze on a screen point (px) and shrink the idle darts to a pixel; off = wander again. */
void eyes_set_attention(eyes_t *e, bool on, int x_px, int y_px);

/* Idle-layer tuning used by animations. */
void eyes_set_idle_rates(eyes_t *e, int32_t blink_interval_scale, int32_t blink_speed_scale, int32_t dart_scale);
/* Start a blink now (if one is not already running). */
void eyes_blink_now(eyes_t *e, uint32_t now_ms);

/*
 * Advance easing and the idle layer to `now_ms`, then produce the two shapes
 * to draw (bounding boxes included).
 */
void eyes_update(eyes_t *e, uint32_t now_ms, raster_shape_t out[2]);

/* Q16 easing, t in [0, ONE]. */
int32_t ease_in_out_q16(int32_t t);
int32_t ease_out_back_q16(int32_t t);
