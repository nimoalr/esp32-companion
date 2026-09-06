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
    bool usb;                   /* on the charger: the microphones can stay on */
    bool dancing;               /* the dance is on by the user's choice: heavy handling is expected */
    /* touch language: counters, so an event is a change */
    int poke_eye;               /* with the latest tap: 0 elsewhere, 1 left eye, 2 right eye */
    uint32_t stroke_count;      /* swipes across the glass */
    bool stroke_forehead;       /* the latest stroke was across the top (petting) */
} behavior_in_t;

typedef enum {
    BEH_EV_NONE = 0,
    BEH_EV_PICKED_UP,
    BEH_EV_PUT_DOWN,
    BEH_EV_BODY_TAP,
} behavior_event_t;

typedef struct {
    int override_anim;          /* anim_id_t to show, or -1 for the user's choice */
    bool knocked_out, zz;
    bool want_mic;              /* behaviour wants the microphones running */
    eye_pose_t env[2];          /* gravity gaze deltas */
    float face_angle_deg;       /* whole-face rotation so it stays upright against gravity */
    behavior_event_t event;     /* one-shot handling event this frame */
    float tap_side;             /* for a body tap: -1 left .. +1 right, where the knock came from */
    int dance_flourish;         /* during a dance: 2 = a stroke's sway, this frame */
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
    BEH_LISTENING,              /* someone is talking: look their way */
    BEH_CARRIED,                /* walking rhythm for a while: content */
    BEH_STARTLED,               /* a knock on the body */
    BEH_POKED,                  /* a tap on the eyes */
    BEH_PETTED,                 /* strokes across the forehead */
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
    uint32_t speech_last_ms;    /* last frame with speech */
    float voice_dir;            /* smoothed direction of the voice along the mic axis */
    /* handling */
    float prev_mag;             /* |a| of the previous sample, g */
    float hp_mag;               /* high-passed |a| for the walking rhythm */
    float hp_prev;
    uint32_t still_since_ms;    /* 0 while moving */
    uint32_t moving_since_ms;   /* 0 while still */
    bool was_resting;           /* still for long enough to count a pick-up */
    uint32_t last_cross_ms;     /* last zero crossing of the rhythm */
    float cross_gap_ms;         /* smoothed interval between crossings */
    float cross_amp;            /* smoothed swing of the rhythm, g */
    uint32_t rhythm_since_ms;   /* when the walking rhythm began, 0 = none */
    uint32_t spike_ms;          /* last body-tap spike */
    float spike_side;
    uint32_t last_touch_ms;     /* a touch this close disqualifies a spike */
    behavior_event_t pending;
    anim_id_t listen_anim;      /* the face while listening, re-rolled now and then */
    uint32_t listen_roll_ms;
    anim_id_t poke_anim;
    /* mood */
    float energy;               /* 0..1 stimulation, Vector's sense: rises with anything happening, decays */
    float valence;              /* -1..1: how well he has been treated lately */
    anim_id_t idle_anim;        /* the face of the moment while nothing happens, from the mood */
    uint32_t idle_roll_ms;
    uint32_t strokes_seen;
    uint32_t last_stroke_ms;
    int poked_eye;              /* 0 none, 1 left, 2 right, during BEH_POKED */
    uint32_t dance_eye_ms;      /* an eye tapped during the dance: that eye squints for a moment */
    int dance_eye;
    uint32_t mood_tick_ms;
    uint32_t rng;
    uint32_t taps_seen;
    /* gravity face */
    float face_angle;           /* current, slew-limited */
    float face_target;
    uint32_t face_ms;
} behavior_t;

float behavior_energy(const behavior_t *b);
float behavior_valence(const behavior_t *b);
/* a nudge to the valence from outside the behaviour (the persona's judgement of an event) */
void behavior_feel(behavior_t *b, float valence_delta);

void behavior_init(behavior_t *b, uint32_t now_ms);
void behavior_update(behavior_t *b, const behavior_in_t *in, uint32_t now_ms, behavior_out_t *out);
const char *behavior_state_name(behavior_state_t s);
