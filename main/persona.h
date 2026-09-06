/*
 * What the character says, and when. Watches the situation (power state, the
 * behaviour's reaction, the expression, touch, charger) and turns changes into
 * utterances: a chirp for a mood, a word for an event, babble when nothing happens
 * for a while. Pure C: decisions out, the caller speaks them.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "anim.h"
#include "behavior.h"
#include "voice.h"
#include "clips_gen.h"

typedef struct {
    bool in_ui;                 /* setup screens open: silent */
    int power;                  /* 0 active, 1 drowsy, 2 asleep */
    behavior_state_t beh;
    anim_id_t anim;             /* what the eyes show */
    float energy;               /* 0..1 mood energy */
    bool finger;                /* a finger resting on the screen */
    uint32_t tap_count;         /* running count of taps on the eyes */
    bool usb;
    int batt_pct;               /* -1 unknown */
    bool speaking;              /* the mouth is busy */
    bool speech;                /* someone is talking */
    behavior_event_t event;     /* one-shot handling event */
    int chattiness;             /* 0 quiet .. 3 talkative */
} persona_in_t;

typedef enum { SAY_NONE, SAY_GESTURE, SAY_WORD, SAY_BABBLE } say_kind_t;

typedef struct {
    say_kind_t kind;
    int id;                     /* voice_id_t or clip index */
    float level;                /* 0..1 */
    float energy;               /* babble */
    bool interrupt;             /* cut whatever is playing */
} persona_say_t;

typedef struct {
    persona_in_t prev;
    bool primed;
    uint32_t rng;
    uint32_t last_say_ms;       /* any utterance */
    uint32_t next_idle_ms;      /* when the next spontaneous one may come */
    uint32_t finger_since_ms;
    bool purred;
    uint32_t taps_ms[4];        /* recent tap times, for pokes */
    int taps_n;
    uint32_t dizzy_ms[4];       /* recent shakings, for the escalation */
    int dizzy_n;
    uint32_t low_batt_ms;
    uint32_t talk_since_ms;     /* the current conversation started */
    uint32_t talk_last_ms;      /* last frame of it */
    bool answered;
    int recent_kind[2], recent_id[2];   /* the last two utterances, not to be repeated */
} persona_t;

void persona_init(persona_t *p, uint32_t now_ms, uint32_t seed);
/* Call every frame; fills *out (SAY_NONE most of the time). */
void persona_tick(persona_t *p, const persona_in_t *in, uint32_t now_ms, persona_say_t *out);
