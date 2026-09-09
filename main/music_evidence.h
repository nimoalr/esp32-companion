/* A second rhythm cue for music whose percussion is above the kick band.
 * Fixed storage and bounded work per 16 ms frame; no allocations or FFTs. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float history[2][256], mean[2], pair[2];
    unsigned cursor, frames, anchor, next_lag;
    float best, support, confidence, bpm, evidence, level;
    unsigned best_lag, previous_lag;
} music_evidence_t;
void music_evidence_reset(music_evidence_t *s);
void music_evidence_update(music_evidence_t *s, float bass, float high,
                           float raw_loud, bool muted);
