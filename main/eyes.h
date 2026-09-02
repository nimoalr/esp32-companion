/*
 * Eye model: base geometry + an animated pose per eye, eased per field, with
 * an always-on idle layer (blink + saccades) applied underneath.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "raster.h"

/* What the renderer draws. All lengths Q16 pixels, fractions Q16 in [0, 1]. */
typedef struct {
    int32_t cx, cy;         /* centre */
    int32_t w, h;           /* full width / height */
    int32_t radius;         /* corner radius */
    int32_t lid_top;        /* fraction of h hidden from the top, at x == cx */
    int32_t lid_bottom;     /* fraction of h hidden from the bottom, at the outer x */
    int32_t slant;          /* dy/dx of the top lid line (+ descends toward +x) */
    int32_t curve;          /* upward bulge of the bottom lid at the centre, pixels */
    uint32_t color;         /* 0xRRGGBB */
} EyeParams;

/* Animated pose, relative to the base geometry. */
#define EYE_POSE_FIELDS 8
typedef union {
    struct {
        int32_t sx, sy;               /* width / height scale, Q16 (ONE = base) */
        int32_t lid_top, lid_bottom;  /* Q16 fractions */
        int32_t slant;                /* Q16 dy/dx */
        int32_t curve;                /* Q16 fraction of the (scaled) height */
        int32_t dx, dy;               /* Q16 px centre offset */
    };
    int32_t v[EYE_POSE_FIELDS];
} eye_pose_t;

#define EYE_POSE_NEUTRAL { { Q16_ONE, Q16_ONE, 0, 0, 0, 0, 0, 0 } }

typedef struct {
    EyeParams base;                    /* cx, cy, w, h, radius, color of the resting eye */
    eye_pose_t cur, from, to;
    uint32_t t0_ms[EYE_POSE_FIELDS];
    uint32_t dur_ms[EYE_POSE_FIELDS];
    uint16_t lut[256];
} EyeState;

typedef struct {
    /* blink */
    uint32_t next_blink_ms;
    uint32_t blink_t0_ms;
    bool blinking;
    int32_t blink_interval_scale;      /* Q16; > ONE = rarer blinks */
    int32_t blink_speed_scale;         /* Q16; > ONE = slower close/open */
    /* saccade */
    uint32_t next_sacc_ms;
    uint32_t sacc_t0_ms;
    int32_t sacc_from_x, sacc_from_y, sacc_to_x, sacc_to_y;
    uint32_t rng;
} eyes_idle_t;

typedef struct {
    EyeState eye[2];                   /* 0 = left (viewer's left), 1 = right */
    eye_pose_t mod[2];                 /* per-frame additive modulation (wobble, bounce, beat); deltas */
    eye_pose_t env[2];                 /* environment layer (gravity gaze), set by the behaviour code; deltas */
    eyes_idle_t idle;
    bool face_rot;                     /* whole face rotated about the screen centre */
    int32_t face_cos, face_sin;        /* Q16 */
} eyes_t;

void eyes_init(eyes_t *e, uint32_t now_ms);

/* Retarget every pose field of one eye; a retarget during a transition restarts from the current value. */
void eyes_set_target(eyes_t *e, int eye, const eye_pose_t *pose, uint32_t dur_ms, uint32_t now_ms);

/* Per-frame additive modulation on top of the eased pose; cleared by eyes_clear_mod(). */
void eyes_clear_mod(eyes_t *e);
void eyes_set_env(eyes_t *e, int eye, const eye_pose_t *delta);

/* Rotate the whole face about the screen centre; degrees, clockwise on screen, 0 = upright. */
void eyes_set_face_angle(eyes_t *e, float deg);

/* Idle-layer tuning used by animations. */
void eyes_set_idle_rates(eyes_t *e, int32_t blink_interval_scale, int32_t blink_speed_scale);

/*
 * Advance easing and the idle layer to `now_ms`, then produce the two shapes
 * to draw (bounding boxes included).
 */
void eyes_update(eyes_t *e, uint32_t now_ms, raster_shape_t out[2]);

/* Q16 smoothstep, t in [0, ONE]. */
int32_t ease_in_out_q16(int32_t t);
