/*
 * Environment-driven character: reacts to being shaken, tilted, put face
 * down, and to music. Sits above the tap-selected expression and overrides it
 * while a reaction lasts. Pure C: inputs in, decisions out, no hardware.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "anim.h"
#include "imu_cal.h"
#include "audio_features.h"

typedef struct {
    bool have_accel;
    int16_t accel[3];           /* raw, new sample when accel_ms changes */
    uint32_t accel_ms;
    const imu_cal_t *cal;
    audio_features_t audio;     /* .active false when the mics are off */
    bool mic_available;         /* board has working mics */
    bool user_interacting;      /* touch in the last few seconds */
    uint32_t tap_count;         /* running count of taps; a tap during a music reaction ends it */
} behavior_in_t;

typedef struct {
    int override_anim;          /* anim_id_t to show, or -1 for the user's choice */
    bool headphones, knocked_out, zz;
    bool want_mic;              /* behaviour wants the microphones running */
    eye_pose_t env[2];          /* gravity gaze deltas */
    float face_angle_deg;       /* whole-face rotation so it stays upright against gravity */
} behavior_out_t;

typedef enum {
    BEH_IDLE = 0,
    BEH_DIZZY,
    BEH_KNOCKED_OUT,
    BEH_GROGGY,
    BEH_FACE_DOWN,
    BEH_WAKING,
    BEH_MUSIC,
    BEH_UNIMPRESSED,
} behavior_state_t;

typedef struct {
    behavior_state_t state;
    uint32_t state_since_ms;
    uint32_t last_accel_ms;
    float shake;                /* smoothed |a| deviation from 1 g, in g */
    float shake_time_ms;        /* accumulated shaking */
    float gx, gy, gz;           /* smoothed screen-frame gravity */
    uint32_t face_down_since_ms;
    /* music sniffing */
    uint32_t next_sniff_ms;
    uint32_t sniff_start_ms;
    bool sniffing;
    uint32_t music_quiet_since_ms;
    bool music_headphones;      /* this time he put the headphones on */
    /* mood */
    float energy;               /* 0..1, drifts; fed by handling and touch */
    uint32_t mood_tick_ms;
    uint32_t rng;
    uint32_t taps_seen;
    /* gravity face */
    float face_angle;           /* current, slew-limited */
    float face_target;
    uint32_t face_ms;
} behavior_t;

float behavior_energy(const behavior_t *b);

void behavior_init(behavior_t *b, uint32_t now_ms);
void behavior_update(behavior_t *b, const behavior_in_t *in, uint32_t now_ms, behavior_out_t *out);
const char *behavior_state_name(behavior_state_t s);
