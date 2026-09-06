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
    /* Append new IDs: existing tap/selection IDs keep their values. */
    ANIM_SMUG,
    ANIM_SUSPICIOUS,
    ANIM_DETERMINED,
    ANIM_PLEADING,
    ANIM_MISCHIEVOUS,
    ANIM_EMBARRASSED,
    ANIM_RELIEVED,
    ANIM_DOUBLE_TAKE,
    ANIM_KNOCKED_OUT,
    ANIM_RECOVERING,
    ANIM_HEARTS,
    ANIM_HEARTBREAK,
    ANIM_HIGH_ROLLER,
    ANIM_NOD,
    ANIM_PEEKABOO,
    ANIM_LOADING,
    ANIM_BOOP,
    ANIM_SNEEZE,
    ANIM_COUNT
} anim_id_t;

typedef struct {
    anim_id_t id;
    uint32_t t_enter_ms;    /* start of the current pass through the keyframes */
    uint32_t t_change_ms;   /* actual selection change, independent of loop wraps */
    eye_symbol_t previous_symbol[2];
    int32_t previous_split;
    int32_t previous_reel[2];
    int32_t previous_gate[2];
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
    int dance_flourish;         /* 0 none, 2 a slow sway (a stroke) */
    float dance_bars[2][8];     /* smoothed spectrum heights, left eye low bands, right eye high */
    int dance_visual;           /* the passing visual on top of the dance: 0 plain, 1 spectrum, 2 mirror ball, 3 spotlights */
    int dance_visual_last;
    float disco_spin;
    uint32_t dance_visual_ms;   /* when the current one began */
    uint32_t dance_visual_len;  /* how long it stays */
    float dance_visual_mix;     /* 0..1 fade of the visual */
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
