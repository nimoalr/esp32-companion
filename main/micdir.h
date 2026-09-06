/*
 * Arrival-time difference of a transient (clap, knock) between the two microphones.
 * Pure C, no hardware: the host harness tools/host/micdir_test.c exercises it.
 *
 * The mics are ~40 mm apart, at most ~1.9 samples of delay at 16 kHz, so the timing
 * has to be sub-sample. A transient is found by its rising edge (a jump from near
 * silence to 30 % of the frame's peak within half a millisecond) and timed by a
 * normalised cross-correlation of the first 2 ms of that edge, refined with a parabola.
 * The edge arrives before the room's reflections and before any clipping, so it is the
 * one robust feature of a clap. The previous frame's tail is kept so an onset on a
 * frame boundary still counts, and is timed whole in the next frame.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MICDIR_HIST     48      /* samples of the previous frame kept */
#define MICDIR_MAX_LAG  3.f     /* clamp, samples */
#define MICDIR_MIN_PEAK 2000    /* a transient must reach this many LSB (either mic) to be timed */

typedef struct {
    int16_t hist[2 * MICDIR_HIST];
    uint16_t n;             /* transients timed */
    uint16_t loud;          /* loud frames that were not an onset (continuations); diagnostic */
    int16_t pre;            /* pre-onset level of the last loud frame, % of its peak; diagnostic */
    bool primed;            /* hist holds a real tail (nothing is timed on the first frame) */
    int32_t refr;           /* index (relative to the current frame) of the last timed onset */
} micdir_t;

void micdir_reset(micdir_t *d);

/*
 * One frame of interleaved stereo (L = slot 0, R = slot 1), `frame` samples, whose
 * largest magnitude is `peak`. Returns true when a transient was timed and fills *out.
 */
typedef struct {
    float lag;              /* arrival-time difference t_R - t_L in samples (positive = reached L first) */
    float balance;          /* 0..1: the quieter mic's peak over the louder one's */
    float corr;             /* 0..1: normalised correlation of the two edges at the best lag */
    int peak;               /* the louder mic's peak in the edge window, LSB */
} micdir_result_t;

bool micdir_frame(micdir_t *d, const int16_t *pcm, int frame, int peak, micdir_result_t *out);
