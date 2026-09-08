#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio_features.h"
typedef enum { POWER_ACTIVE, POWER_DROWSY, POWER_SLEEP, POWER_DEEP } power_state_t;
/* Admission/exit and audible breakdown grace belong to behavior. Once admitted,
 * every session frame counts as activity, including frames without a fresh kick. */
static inline bool power_music_present(const audio_features_t *a, bool admitted)
{ return admitted && a->active; }
static inline bool power_idle_expired(uint32_t now, uint32_t activity, uint32_t duration)
{ return (int32_t)(now-activity) >= (int32_t)duration; }
static inline power_state_t power_policy_next(power_state_t state, uint32_t now, uint32_t activity,
                                              uint32_t since, uint32_t active_ms, uint32_t dim_ms)
{
    if(state==POWER_ACTIVE && power_idle_expired(now,activity,active_ms))return POWER_DROWSY;
    if(state==POWER_DROWSY) {
        if((int32_t)(activity-since)>0)return POWER_ACTIVE;
        if(power_idle_expired(now,since,dim_ms))return POWER_SLEEP;
    }
    return state;
}
