/* What the audio analyser publishes for the dance choreography. Pure data. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active;            /* analyser running */
    float bass, mid, high;  /* band levels, 0..1 after automatic gain */
    float loud;             /* overall level, 0..1 */
    float balance;          /* -1 = left mic louder .. +1 = right mic louder */
    uint32_t beat_count;    /* increments on each bass onset */
    uint32_t last_beat_ms;
    float bpm;              /* 0 until enough beats have been seen */
    uint32_t cpu_us;        /* analysis time of the last frame, for the stats line */
} audio_features_t;
