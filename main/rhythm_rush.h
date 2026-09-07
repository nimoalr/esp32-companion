/* Short, faster bass subdivisions after a confirmed groove. No music admission. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float reference_ms, pulse_ms, bpm;
    uint32_t lock_since_ms, locked_ms, last_ms, event_ms, count;
    unsigned consistent;
} rhythm_rush_t;
void rhythm_rush_update(rhythm_rush_t *r,uint32_t now,bool onset,float locked_gap,float bass_ratio,bool muted);
