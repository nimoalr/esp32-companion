#include <assert.h>
#include <stdio.h>
#include "power_policy.h"
#include "behavior.h"
uint32_t esp_random(void){return 17;}
int main(void)
{
    for(unsigned boot=1000;boot<=100000;boot+=99000) {
        power_state_t state=POWER_ACTIVE;uint32_t activity=boot,since=boot,slept=0;
        audio_features_t a={.active=true,.raw_loud=500,.tempo_conf=.95f,.bass_ratio=.3f};
        for(uint32_t t=boot;t<boot+3*31800000;t+=100) {
            a.last_beat_ms=t; /* A room full of false candidate/beat pulses, never admitted music. */
            if(power_music_present(&a,false))activity=t;
            power_state_t next=power_policy_next(state,t,activity,since,30000,1800000);
            if(next!=state){state=next;since=t;if(state==POWER_SLEEP)slept=t;}
        }
        assert(state==POWER_SLEEP&&slept==boot+1830000);
        assert(power_policy_next(POWER_DROWSY,boot+40000,boot+39990,boot+30000,30000,1800000)==POWER_ACTIVE);
        assert(power_policy_next(POWER_ACTIVE,boot,boot+16,boot,30000,1800000)==POWER_ACTIVE);
        assert(power_music_present(&a,true));
        a.tempo_conf=.3;assert(power_music_present(&a,true));
        a.raw_loud=0;a.last_beat_ms=0;assert(power_music_present(&a,true));
    }
    assert(!power_idle_expired(0xfffffff0u,0xfffffff8u,30000));
    assert(power_idle_expired(30000,0xfffffff0u,30000));
    /* A confirmed session stays awake for hours without any fresh onset or
     * confidence requirement in the power layer. Exit starts a fresh deadline. */
    uint32_t activity=1000,since=1000;power_state_t state=POWER_ACTIVE;
    audio_features_t a={.active=true};
    for(uint32_t t=1000;t<10801000;t+=100) {
        if(power_music_present(&a,true))activity=t;
        assert(power_policy_next(state,t,activity,since,30000,1800000)==POWER_ACTIVE);
    }
    assert(power_policy_next(POWER_ACTIVE,activity+29999,activity,since,30000,1800000)==POWER_ACTIVE);
    assert(power_policy_next(POWER_ACTIVE,activity+30000,activity,since,30000,1800000)==POWER_DROWSY);
    assert(power_policy_next(POWER_DROWSY,2000,2000,1000,30000,1800000)==POWER_ACTIVE);
    behavior_t b;behavior_init(&b,1000);behavior_out_t out;
    behavior_in_t in={.dozing=true,.usb=true,.audio={.active=true,.speech=true}};
    for(uint32_t t=2000;t<1830000;t+=1000){in.unattended_ms=t-1000;behavior_update(&b,&in,t,&out);}
    assert(b.valence<-.3f&&b.valence>-.6f&&b.energy<.3f);
    assert(behavior_doze_face(40000)==ANIM_BORED);
    assert(behavior_doze_face(64000)==ANIM_PLEADING);
    bool seen[ANIM_COUNT]={0};unsigned naps=0,wakes=0;anim_id_t prev=ANIM_NEUTRAL;
    for(uint32_t t=90000;t<1830000;t+=1000) {
        anim_id_t face=behavior_doze_face(t);seen[face]=true;
        if(face==ANIM_SLEEPING && prev!=face)naps++;
        if(face==ANIM_SLEEPY && prev==ANIM_SLEEPING)wakes++;
        prev=face;
    }
    assert(naps>=14&&wakes>=14&&seen[ANIM_BORED]&&seen[ANIM_SAD]&&seen[ANIM_YAWN]);
    behavior_init(&b,1000);b.state=BEH_MUSIC;b.music_quiet_since_ms=1000;
    in=(behavior_in_t){.idle_allowed=true,.audio={.active=true,.raw_loud=300}};
    for(uint32_t t=1000;t<37000;t+=16) {
        behavior_update(&b,&in,t,&out);assert(b.state==BEH_MUSIC);
        assert(power_music_present(&in.audio,b.state==BEH_MUSIC));
    }
    puts("PASS 30-minute dim deadline, three-hour music wake hold, audible breakdown, fresh countdown after music, false-onset rejection, touch wake, timestamp wrap and varied nap/wake cycles");
}
