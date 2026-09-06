/* What the audio analyser publishes for the dance choreography. Pure data. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active;            /* analyser running */
    float bass, mid, high;  /* band levels, 0..1 after automatic gain */
    float kick;             /* sub-bass (below ~80 Hz) level, 0..1: the kick drum's body */
    float loud;             /* overall level, 0..1 */
    float raw_loud;         /* overall level before automatic gain: RMS of the 16-bit samples, in LSB */
    int16_t peak;           /* largest sample magnitude in the last frame */
    float balance;          /* -1 = left mic louder .. +1 = right mic louder */
    uint32_t beat_count;    /* increments on each bass onset */
    uint32_t last_beat_ms;
    float bpm;              /* 0 until enough beats have been seen */
    float regularity;       /* 0..1: how even the recent beat intervals are (1 = metronome) */
    float tempo_conf;       /* 0..1: share of recent intervals within 12 % of the median tempo (octave-folded) */
    float bass_ratio;       /* sub-bass RMS / full-band RMS, smoothed over ~1 s: speech ~0.1, EDM 0.3-0.7 */
    float dir;              /* -1..+1 along the microphone axis, smoothed: + = the sound reaches MIC2 (TDM slot 2) first */
    float dir_conf;         /* 0..1: normalised cross-correlation peak of the last confident frame */
    float dir_lag;          /* raw lag in samples of that frame (positive = MIC2 leads) */
    uint32_t cpu_us;        /* analysis time of the last frame, for the stats line */
} audio_features_t;
