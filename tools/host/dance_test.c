/* Compositor occlusion/damage, refresh bounds, texture rotation, and dance timing. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "dance_lasers.h"
uint32_t esp_random(void){return 17;}
static uint16_t plain[466*466], before[466*466], after[466*466];
static eyes_t eyes;
static raster_shape_t shapes[2];
static dance_lasers_t lasers;
static void paint(uint16_t *fb,const dance_lasers_t *l)
{
    for(int y=0;y<466;y+=16) dance_lasers_paint(l,fb+y*466,0,y,466,466-y<16?466-y:16,shapes);
}
static void compositor(void)
{
    eyes_init(&eyes,0);eyes_set_hotspot(&eyes,true);eyes_set_idle_rates(&eyes,0,Q16_ONE,0);
    eyes_update(&eyes,16,shapes);paint(plain,NULL);memcpy(before,plain,sizeof plain);
    unsigned changed_pixels=0;
    for(int f=0;f<90;f++) {
        assert(dance_lasers_update(&lasers,1,(uint32_t)f/3,(uint32_t)f*40+1000,(float)f*4));
        paint(after,&lasers);
        for(int y=0;y<466;y++)for(int x=0;x<466;x++) {
            const int i=y*466+x;
            /* Every change fits the damage handed to the display, including old beams. */
            if(after[i]!=before[i]) {
                assert(x>=lasers.damage[0]&&x<lasers.damage[2]&&y>=lasers.damage[1]&&y<lasers.damage[3]);changed_pixels++;
            }
            /* Interior eye pixels are opaque even where a laser crosses the eye. */
            bool interior=x>=2&&x<464&&y>=2&&y<464;
            if(interior)for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)if(!plain[i+dy*466+dx])interior=false;
            if(interior)assert(after[i]==plain[i]);
        }
        memcpy(before,after,sizeof before);
    }
    assert(changed_pixels>10000);
    assert(dance_lasers_update(&lasers,0,0,5000,0));paint(after,&lasers);
    assert(!memcmp(after,plain,sizeof plain));
    for(int y=0;y<466;y++)for(int x=0;x<466;x++)if(after[y*466+x]!=before[y*466+x])
        assert(x>=lasers.damage[0]&&x<lasers.damage[2]&&y>=lasers.damage[1]&&y<lasers.damage[3]);
    assert(!dance_lasers_update(&lasers,0,0,5001,0));
    unsigned updates=0;
    for(uint32_t t=6000;t<7000;t++)updates+=dance_lasers_update(&lasers,1,t/400,t,0);
    assert(updates==30);
}
static float timing(uint32_t step)
{
    anim_sm_t sm;eyes_init(&eyes,1000);anim_init(&sm,&eyes,1000);anim_set(&sm,&eyes,ANIM_DANCE,1000);
    sm.dance_visual=2;sm.dance_visual_len=100000;
    audio_features_t a={.active=true,.loud=.6f,.bass=.6f,.kick=.8f,.bpm=150,.regularity=1};
    for(uint32_t t=1000;t<=7400;t+=step){
        /* Last kick at 1800; the next 5.6 seconds have sound but no beat. */
        if(t<2000){a.beat_count=1+(t-1000)/400;a.last_beat_ms=1000+(t-1000)/400*400;}
        anim_set_audio(&sm,&a);anim_update(&sm,&eyes,t);
    }
    assert(sm.dance_beats_seen==3&&sm.dance_beat_ms==1800); /* no invented kicks */
    const float spin=sm.disco_spin;
    anim_set(&sm,&eyes,ANIM_NEUTRAL,7500);assert(eyes.laser_mix==0);
    return spin;
}
int main(void)
{
    dance_fill_t a,b;dance_fill_disco(&a,0);dance_fill_disco(&b,1);assert(!memcmp(&a,&b,sizeof a));
    dance_fill_disco(&b,.13f);assert(memcmp(&a,&b,sizeof a));
    for(int i=0;i<4096;i++)assert(a.tex[i]<=31&&b.tex[i]<=31);
    compositor();float fast=timing(16),slow=timing(32);assert(fabsf(fast-slow)<.01f);
    printf("PASS: laser occlusion, rotated damage + switch-off, 30 Hz cap, texture turn, coast timing (spin %.4f / %.4f)\n",fast,slow);
    printf("RAM: texture=%zu, laser snapshot=%zu, eye state=%zu bytes (host pointer sizes)\n",sizeof(dance_fill_t),sizeof(dance_lasers_t),sizeof(eyes_t));
}
