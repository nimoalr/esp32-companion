#pragma once

#include <stdint.h>
#include "eyes.h"

typedef enum {
    ANIM_NEUTRAL = 0,
    ANIM_HAPPY,
    ANIM_SAD,
    ANIM_ANGRY,
    ANIM_SURPRISED,
    ANIM_SLEEPY,
    ANIM_LOOK_AROUND,
    ANIM_WINK,
    ANIM_COUNT
} anim_id_t;

typedef struct {
    anim_id_t id;
    uint32_t t_enter_ms;    /* start of the current pass through the keyframes */
    int next_kf;            /* index of the next keyframe to apply */
} anim_sm_t;

void anim_init(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
/* Tap: advance to the next animation, wrapping to NEUTRAL after WINK. */
void anim_next(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
/* Previous animation, wrapping. */
void anim_prev(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
/* Jump to a specific animation (used by the power state machine). */
void anim_set(anim_sm_t *sm, eyes_t *eyes, anim_id_t id, uint32_t now_ms);
/* Apply keyframes whose time has come. Call once per frame before eyes_update(). */
void anim_update(anim_sm_t *sm, eyes_t *eyes, uint32_t now_ms);
const char *anim_name(anim_id_t id);
