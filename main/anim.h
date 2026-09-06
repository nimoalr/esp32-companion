#pragma once

#include <stdint.h>
#include "eyes.h"
#include "audio_features.h"

typedef enum {
    ANIM_NEUTRAL = 0,
    ANIM_HAPPY,
    ANIM_SAD,
    ANIM_ANGRY,
    ANIM_SURPRISED,
    ANIM_SLEEPY,
    ANIM_LOOK_AROUND,
    ANIM_WINK,
    ANIM_CURIOUS,
    ANIM_CONFUSED,
    ANIM_LOVE,
    ANIM_DIZZY,
    ANIM_LAUGHING,
    ANIM_SCARED,
    ANIM_SKEPTICAL,
    ANIM_THINKING,
    ANIM_BORED,
    ANIM_EXCITED,
    ANIM_SHY,
    ANIM_ANNOYED,
    ANIM_SLEEPING,
    ANIM_SQUINT,
    ANIM_DANCE,             /* procedural, driven by audio_features_t */
    ANIM_COUNT
} anim_id_t;

typedef struct {
    anim_id_t id;
    uint32_t t_enter_ms;    /* start of the current pass through the keyframes */
    int next_kf;            /* index of the next keyframe to apply */
    /* jitter modulator state */
    int32_t jit_from[2][EYE_POSE_FIELDS], jit_to[2][EYE_POSE_FIELDS];
    uint32_t jit_t0_ms;
    uint32_t rng;
    /* dance state */
    audio_features_t audio;
    uint32_t dance_beats_seen;
    uint32_t dance_beat_ms;
    int dance_side;         /* +1 / -1, alternates on beats */
    float dance_bass, dance_loud, dance_bal;   /* smoothed */
    uint32_t dance_last_sound_ms;
    int dance_flourish;         /* 0 none, 1 bounce burst (a poke), 2 shimmy (a stroke), 3 wink */
    uint32_t dance_flourish_ms; /* when it started */
} anim_sm_t;

void anim_init(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
/* Tap: advance to the next animation, wrapping. */
void anim_next(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
/* Previous animation, wrapping. */
void anim_prev(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
/* Jump to a specific animation (used by the power state machine and the UI). */
void anim_set(anim_sm_t *sm, eyes_t *eyes, anim_id_t id, uint32_t now_ms);
/* A touch during the dance: a move on top of the beat instead of an interruption. */
void anim_dance_flourish(anim_sm_t *sm, int kind, uint32_t now_ms);

/* Latest audio features for ANIM_DANCE; call before anim_update(). */
void anim_set_audio(anim_sm_t *sm, const audio_features_t *f);
/* Apply keyframes whose time has come and compute this frame's modulation. Call once per frame before eyes_update(). */
void anim_update(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
const char *anim_name(anim_id_t id);
