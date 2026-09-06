/* What the audio analyser publishes for the dance choreography. Pure data. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Microphone axis calibration from the setup wizard: maps a raw arrival-time difference
 * in samples to -1..+1, + = the lanyard end, - = the USB end. */
typedef struct {
    bool valid;
    float offset;           /* lag of a sound facing the screen (equidistant from both mics) */
    float gain;             /* signed, per sample: dir = (lag - offset) * gain */
    float sep;              /* lag difference between the two ends, samples (diagnostic) */
} mic_cal_t;

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
    float dir;              /* -1..+1 along the microphone axis, smoothed: + = the sound reaches MIC1 (slot 0) first */
    float dir_conf;         /* 0..1: how evenly the two mics heard the last transient (level balance) */
    float dir_lag;          /* arrival-time difference of the last transient in samples (+ = MIC1 first) */
    float dir_corr;         /* 0..1: how alike the two mics' edges were for the last transient */
    int dir_peak;           /* the last transient's peak, LSB */
    uint16_t dir_n;         /* transients (claps, knocks) timed since the mics started */
    uint16_t dir_loud;      /* loud frames that were not onsets (diagnostic) */
    int16_t dir_pre;        /* pre-onset level of the last loud frame, % of its peak (diagnostic) */
    float rms_l, rms_r;     /* per-channel RMS in LSB: MIC1 (slot 0) and MIC2 (slot 1) */
    uint32_t cpu_us;        /* analysis time of the last frame, for the stats line */
} audio_features_t;
