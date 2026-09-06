/*
 * Procedural voice: short wordless utterances (the R2-D2 / WALL-E school) described
 * by a few numbers each, synthesised in real time. Pure C, no hardware: the host
 * renderer tools/host/voice_render.c writes the vocabulary to WAV files.
 *
 * Two oscillators (a fundamental with a little harmonic colour, and a partner a fifth
 * or an octave up), a pitch contour of up to four breakpoints, vibrato, a trill, a
 * breath of filtered noise, an attack/release envelope with optional staccato pulses,
 * and a one-pole low-pass. Every utterance gets a random detune and duration jitter
 * so nothing repeats exactly. Mono, 16 kHz, 16-bit.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VOICE_RATE 16000

typedef enum {
    VOICE_HAPPY,        /* rising two-note chirp */
    VOICE_LAUGH,        /* the chirp with staccato pulses */
    VOICE_SAD,          /* falling slide */
    VOICE_SURPRISED,    /* quick rising whoop */
    VOICE_SCARED,       /* rising whoop with a trill */
    VOICE_ANGRY,        /* low buzz */
    VOICE_ANNOYED,      /* short grumble */
    VOICE_YAWN,         /* long descending glide */
    VOICE_PURR,         /* low pulsing tone; loops while the touch lasts */
    VOICE_HM,           /* rising "hm?" */
    VOICE_CONFUSED,     /* two-step "hm? hm?" */
    VOICE_DIZZY,        /* wobbly vibrato slide */
    VOICE_PROTEST,      /* shaken: quick cross chirps */
    VOICE_KO,           /* knocked out: falling tone */
    VOICE_OH,           /* picked up: small "oh" */
    VOICE_BLIP,         /* tap */
    VOICE_WAKE,         /* waking: small rising stretch */
    VOICE_COUNT
} voice_id_t;

typedef enum { VOICE_REG_LOW, VOICE_REG_MID, VOICE_REG_HIGH } voice_register_t;

typedef struct {
    float t, st;                /* time 0..1 of the utterance, pitch in semitones from the register's base */
} voice_pt_t;

typedef struct {
    const char *name;
    float dur_ms;
    int npts;
    voice_pt_t pts[4];
    float vib_hz, vib_st;       /* vibrato rate and depth */
    float trill_hz, trill_st;   /* square alternation (0 = none) */
    float noise;                /* 0..1 breath mix */
    float partner;              /* second oscillator level */
    float partner_ratio;        /* its frequency ratio (1.5 = a fifth, 2 = an octave) */
    float attack_ms, release_ms;
    int pulses;                 /* staccato pulses over the utterance (0 = none) */
    float lp_hz;                /* one-pole low-pass cutoff */
    float level;                /* 0..1 */
    bool loop;                  /* keeps going until voice_stop */
} voice_gesture_t;

typedef struct {
    uint32_t rng;
    voice_register_t reg;
    /* current utterance */
    const voice_gesture_t *g;
    int pos, len;               /* samples */
    float detune;               /* multiplier */
    float ph1, ph2, phv, pht;   /* oscillator, vibrato and trill phases, 0..1 */
    float lp;                   /* filter state */
    float nz;                   /* noise filter state */
    float gain;                 /* volume for this utterance (energy) */
    bool stopping;              /* release requested (loops) */
    float fade;                 /* 1 -> 0 on stop */
} voice_t;

extern const voice_gesture_t k_voice_gestures[VOICE_COUNT];

void voice_init(voice_t *v, uint32_t seed);
void voice_set_register(voice_t *v, voice_register_t reg);
/* Start an utterance; level 0..1 scales its volume (mood energy). A running one is replaced. */
void voice_start(voice_t *v, voice_id_t id, float level);
/* Ask a looping utterance to end (short fade); others are left to finish. */
void voice_stop(voice_t *v);
bool voice_active(const voice_t *v);
/* Render n mono samples; returns how many carry sound (0 = idle, the buffer is zeroed then). */
int voice_render(voice_t *v, int16_t *out, int n);
