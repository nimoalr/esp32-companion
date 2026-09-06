/* Compositor occlusion/damage, refresh bounds, texture rotation, and dance timing. */
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "dance_lasers.h"
uint32_t esp_random(void){return 17;}
static uint16_t plain[466*466], before[466*466], after[466*466];
static eyes_t eyes;
static raster_shape_t shapes[2];
static dance_lasers_t lasers;
static audio_features_t music={.loud=.7f,.bass=.6f,.kick=.8f};
static void paint(uint16_t *fb,const dance_lasers_t *l)
{
    for(int y=0;y<466;y+=16) dance_lasers_paint(l,fb+y*466,0,y,466,466-y<16?466-y:16,shapes);
}
static void compositor(int fx)
{
    eyes_init(&eyes,0);eyes_set_hotspot(&eyes,true);eyes_set_idle_rates(&eyes,0,Q16_ONE,0);
    memset(&lasers,0,sizeof lasers);
    const float bars[8]={.1,.5,.9,.2,.7,.3,.6,.4}, sx[2]={-.5,.4},sy[2]={0,.2};
    eyes_set_bar_heights(&eyes,0,bars);eyes_set_bar_heights(&eyes,1,bars);
    eyes_set_spots(&eyes,2,sx,sy,.3);eyes_set_disco(&eyes,.13,0);
    eyes_set_face_angle(&eyes,fx*11.f);eyes_set_fx(&eyes,fx,1);
    eyes_update(&eyes,16,shapes);paint(plain,NULL);memcpy(before,plain,sizeof plain);
    unsigned changed_pixels=0;
    for(int f=0;f<90;f++) {
        music.beat_count=f/3;music.last_beat_ms=(f/3)*120+1000;
        assert(dance_lasers_update(&lasers,1,&music,(uint32_t)f*40+1000,(float)f*4));
        for(int k=0;k<DANCE_LASER_MAX;k++)if(lasers.beam[k].light) {
            const dance_ray_t *ray=&lasers.beam[k];
            assert(ray->origin_x==35+(k/3)*396/(DANCE_LASER_EMITTERS-1));
            const int major=ray->steep?233:ray->origin_x,minor=ray->steep?ray->origin_x:233;
            assert(ray->major0<=major && ray->major1>=major);
            assert(abs(ray->minor0+(major-ray->major0)*ray->step-minor*Q16_ONE)<512);
        }
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
    assert(dance_lasers_update(&lasers,0,&music,5000,0));paint(after,&lasers);
    assert(!memcmp(after,plain,sizeof plain));
    for(int y=0;y<466;y++)for(int x=0;x<466;x++)if(after[y*466+x]!=before[y*466+x])
        assert(x>=lasers.damage[0]&&x<lasers.damage[2]&&y>=lasers.damage[1]&&y<lasers.damage[3]);
    assert(!dance_lasers_update(&lasers,0,&music,5001,0));
    unsigned updates=0;
    for(uint32_t t=6000;t<7000;t++)updates+=dance_lasers_update(&lasers,1,&music,t,0);
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
static void independent_timer(void)
{
    static eyes_t other;
    anim_sm_t a,b;
    eyes_init(&eyes,1000);eyes_init(&other,1000);
    anim_init(&a,&eyes,1000);anim_init(&b,&other,1000);
    anim_set(&a,&eyes,ANIM_DANCE,1000);anim_set(&b,&other,ANIM_DANCE,1000);
    bool seen=false,ended=false;unsigned overlap=0;uint32_t started=0;
    for(uint32_t t=1000;t<101000;t+=32) {
        audio_features_t af={.active=true,.loud=.8f,.bass=.7f,.kick=.8f,.bpm=150,
            .beat_count=t/400,.last_beat_ms=t-t%400};
        /* One character uses its normal fill schedule; the other changes fills
         * every 4 s. Their background schedules must remain identical. */
        b.dance_visual=(t/4000)%4;b.dance_visual_ms=t;b.dance_visual_len=100000;
        anim_set_audio(&a,&af);anim_set_audio(&b,&af);
        anim_update(&a,&eyes,t);anim_update(&b,&other,t);
        assert(a.dance_lasers_on==b.dance_lasers_on && a.dance_laser_len==b.dance_laser_len);
        assert(fabsf(eyes.laser_mix-other.laser_mix)<.00001f);
        if(a.dance_lasers_on) {
            assert(a.dance_laser_len>=30000&&a.dance_laser_len<60000);
            if(!seen){seen=true;started=t;}
            if(other.laser_mix>.9f)overlap|=1u<<b.dance_visual;
        } else if(seen&&!ended){assert(t-started>=30000&&t-started<61532);ended=true;}
    }
    assert(seen&&ended&&overlap==15);
    anim_set(&a,&eyes,ANIM_NEUTRAL,102000);assert(eyes.laser_mix==0);
}
static void reactivity_and_tiles(void)
{
    static dance_lasers_t quiet,loud;
    audio_features_t af={.loud=.1f,.bass=.1f,.kick=0,.beat_count=20,.last_beat_ms=1000};
    dance_lasers_update(&quiet,1,&af,1000,0);
    af.loud=af.bass=af.kick=1;
    dance_lasers_update(&loud,1,&af,1000,0);
    unsigned q=0,l=0;
    for(int i=0;i<DANCE_LASER_MAX;i++){q+=quiet.beam[i].light;l+=loud.beam[i].light;}
    assert(l>q*2);
    paint(before,&loud);
    static uint16_t tile[137*7];
    for(int y=0;y<466;y+=7)for(int x=0;x<466;x+=137) {
        int w=466-x<137?466-x:137,rows=466-y<7?466-y:7;
        dance_lasers_paint(&loud,tile,x,y,w,rows,shapes);
        for(int row=0;row<rows;row++)memcpy(after+(y+row)*466+x,tile+row*w,w*sizeof *tile);
    }
    assert(!memcmp(before,after,sizeof before));
    unsigned changes=0;uint8_t was[DANCE_LASER_MAX];
    for(int i=0;i<DANCE_LASER_MAX;i++)was[i]=loud.beam[i].light;
    for(uint32_t t=1040;t<12000;t+=40){
        af.beat_count=t/400;af.last_beat_ms=t-t%400;
        dance_lasers_update(&loud,1,&af,t,0);
        for(int i=0;i<DANCE_LASER_MAX;i++){
            if(was[i]>=7 && loud.beam[i].light==0)changes++;
            was[i]=loud.beam[i].light;
        }
    }
    assert(changes>0); /* Individual rays extinguish while the show continues. */
}
int main(void)
{
    dance_fill_t a,b;dance_fill_disco(&a,0);dance_fill_disco(&b,1);assert(!memcmp(&a,&b,sizeof a));
    dance_fill_disco(&b,.13f);assert(memcmp(&a,&b,sizeof a));
    for(int i=0;i<4096;i++)assert(a.tex[i]<=31&&b.tex[i]<=31);
    for(int fx=0;fx<=3;fx++)compositor(fx);
    independent_timer();reactivity_and_tiles();
    float fast=timing(16),slow=timing(32);assert(fabsf(fast-slow)<.01f);
    printf("PASS: all-fill laser occlusion, horizontal emitter row, rotated damage + switch-off, tiles, audio response, independent 30-60 s timer, 30 Hz cap, texture turn, coast timing (spin %.4f / %.4f)\n",fast,slow);
    printf("RAM: texture=%zu, laser snapshot=%zu, eye state=%zu bytes (host pointer sizes)\n",sizeof(dance_fill_t),sizeof(dance_lasers_t),sizeof(eyes_t));
}
