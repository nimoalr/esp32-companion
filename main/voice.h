/*
 * Procedural voice: short wordless utterances (the R2-D2 / WALL-E school) synthesised
 * in real time. Pure C, no hardware: tools/host/voice_render.c writes the vocabulary
 * to WAV files.
 *
 * An utterance is a sequence of syllables. Each syllable has a duration, a pitch
 * contour (start, middle, end, in semitones from the register's base), a vowel colour
 * (two formant resonators over a harmonic-rich source) and an onset consonant (a
 * breath, a click, a pop, a glide). The whole utterance has vibrato, trill, breath,
 * a partner oscillator, an envelope with optional staccato pulses and a low-pass. Every
 * call gets a random detune and duration jitter so nothing repeats exactly. Besides
 * the fixed vocabulary there is a babbler that composes random syllables for idle
 * chatter. Mono, 16 kHz, 16-bit.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VOICE_RATE 16000
#define VOICE_MAX_SEG 6

typedef enum {
    /* moods and reactions */
    VOICE_HAPPY, VOICE_LAUGH, VOICE_SAD, VOICE_SURPRISED, VOICE_SCARED, VOICE_ANGRY, VOICE_ANNOYED,
    VOICE_YAWN, VOICE_PURR, VOICE_HM, VOICE_CONFUSED, VOICE_DIZZY, VOICE_PROTEST, VOICE_KO, VOICE_OH,
    VOICE_BLIP, VOICE_WAKE,
    /* words: two syllables or more, the melody and the vowels carry them */
    VOICE_HELLO, VOICE_UHOH, VOICE_WOW, VOICE_OHNO, VOICE_OKAY, VOICE_BYEBYE, VOICE_OOPSIE, VOICE_REALLY,
    VOICE_SERIOUSLY, VOICE_WHATEVER, VOICE_NOWAY, VOICE_THANKYOU, VOICE_HOORAY, VOICE_SORRY, VOICE_PEEKABOO,
    VOICE_BINGO, VOICE_WAKEYWAKEY, VOICE_GOODNIGHT, VOICE_GOODMORNING, VOICE_OOHLALA, VOICE_AHA, VOICE_COMEON,
    VOICE_EXCUSEME, VOICE_HOWRUDE, VOICE_YUMMY, VOICE_BRAVO, VOICE_HITHERE, VOICE_OHREALLY,
    VOICE_COUNT
} voice_id_t;

typedef enum { VOICE_REG_LOW, VOICE_REG_MID, VOICE_REG_HIGH } voice_register_t;

enum { VOW_NONE, VOW_A, VOW_E, VOW_I, VOW_O, VOW_U, VOW_M, VOW_COUNT };    /* M = closed hum */
enum { ON_NONE, ON_H, ON_K, ON_B, ON_W, ON_L };   /* breath, click, pop, glide up, liquid */

typedef struct {
    float ms;                   /* duration */
    float st0, st1, st2;        /* pitch at start, middle, end, semitones from the base */
    uint8_t vowel, vowel2;      /* vowel2 != 0: a diphthong, the colour glides to it over the second half */
    uint8_t onset;
    float gap_ms;               /* silence after */
} voice_seg_t;

typedef struct {
    const char *name;
    int nseg;
    voice_seg_t seg[VOICE_MAX_SEG];
    float vib_hz, vib_st;       /* vibrato rate and depth */
    float trill_hz, trill_st;   /* square alternation (st 0 = amplitude flutter, the purr) */
    float breath;               /* 0..1 noise under the tone */
    float partner;              /* second oscillator level */
    float partner_ratio;        /* its frequency ratio (1.5 = a fifth, 2 = an octave) */
    float attack_ms, release_ms;
    int pulses;                 /* staccato pulses per syllable (0 = none) */
    float lp_hz;                /* one-pole low-pass cutoff */
    float level;                /* 0..1 */
    bool loop;                  /* keeps going until voice_stop */
} voice_gesture_t;

typedef struct {
    uint32_t rng;
    voice_register_t reg;
    /* current utterance (a copy: the babbler composes on the fly) */
    voice_gesture_t g;
    bool on;
    int seg;                    /* current syllable */
    int pos, len;               /* samples into it / its length */
    int gap;                    /* samples of silence left after it */
    int total, elapsed;         /* whole utterance, samples */
    float detune, detune_len;   /* pitch and duration jitter for this utterance */
    float ph1, ph2, phv, pht;   /* oscillator, vibrato and trill phases, 0..1 */
    float lp, nz, nz2;          /* filter states */
    float f1, f2;               /* formant targets, smoothed */
    float b1[4], b2[4];         /* two band-pass biquad states: x1 x2 y1 y2 */
    float c1[5], c2[5];         /* their coefficients */
    int coef_ctr;
    float gain;
    bool stopping;
    float fade;
} voice_t;


extern const voice_gesture_t k_voice_gestures[VOICE_COUNT];

void voice_init(voice_t *v, uint32_t seed);
void voice_set_register(voice_t *v, voice_register_t reg);
/* Start an utterance; level 0..1 scales its volume (mood energy). A running one is replaced. */
void voice_start(voice_t *v, voice_id_t id, float level);
/* Compose and start a random babble: 1-4 syllables. energy 0..1 sets tempo and pitch spread. */
void voice_babble(voice_t *v, float level, float energy);
/* Ask a looping utterance to end (short fade); others are left to finish. */
void voice_stop(voice_t *v);
bool voice_active(const voice_t *v);
/* Render n mono samples; returns how many carry sound (0 = idle, the buffer is zeroed then). */
int voice_render(voice_t *v, int16_t *out, int n);
