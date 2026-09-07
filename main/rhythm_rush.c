#include "rhythm_rush.h"
#include <math.h>
void rhythm_rush_update(rhythm_rush_t *r,uint32_t now,bool onset,float locked_gap,float bass_ratio,bool muted)
{
    if(muted || bass_ratio<.08f) { r->last_ms=0;r->consistent=0;r->reference_ms=0;r->locked_ms=0;r->lock_since_ms=0;return; }
    if(locked_gap>0) {
        if(!r->lock_since_ms)r->lock_since_ms=now;
        if(now-r->lock_since_ms>=2500)r->locked_ms=now;
    } else r->lock_since_ms=0;
    if(!onset)return;
    uint32_t gap=now-r->last_ms;
    if(r->last_ms && gap<96)return; /* one bass attack can span adjacent frames */
    bool usable=r->last_ms && gap<=1200;
    r->last_ms=now;
    if(!usable){r->consistent=0;r->pulse_ms=0;r->reference_ms=0;return;}
    if(r->consistent && fabsf(gap-r->pulse_ms)<=.14f*r->pulse_ms) {
        r->consistent++;r->pulse_ms+=(gap-r->pulse_ms)*.35f;
    } else {r->pulse_ms=(float)gap;r->consistent=1;}
    if(r->consistent<3)return;
    /* Learn the actual candidate rhythm, including ordinary offbeats, even before
     * music locks. A fast but unchanging subdivision is not a tempo change. */
    bool faster=r->reference_ms>0 && r->pulse_ms<r->reference_ms*.82f;
    if(faster && r->locked_ms && now-r->locked_ms<=8000 &&
       (!r->event_ms || now-r->event_ms>=8000)) {
        r->count++;r->event_ms=now;r->bpm=60000.f/r->pulse_ms;
    }
    r->reference_ms=r->pulse_ms; /* sustained new rate fires once; slower return rearms */
}
