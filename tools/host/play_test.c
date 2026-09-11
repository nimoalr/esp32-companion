#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "behavior.h"
#include "play.h"
#include "sfx.h"
uint32_t esp_random(void){return 71;}
static uint16_t full[466*466],tile[466*466];
static eyes_t eyes;static raster_shape_t shapes[2];
static void pet_test(void)
{
    petting_t p={0};
    for(unsigned t=1000;t<5000;t+=16)petting_update(&p,true,230+50*sinf(t*.006f),70,t);
    assert(p.count>=6);unsigned n=p.count;
    for(unsigned t=5000;t<8000;t+=16)petting_update(&p,true,230+2*sinf(t*.006f),70,t);
    assert(p.count<=n+1);n=p.count;
    for(unsigned t=8000;t<10000;t+=16)petting_update(&p,true,230+50*sinf(t*.006f),250,t);
    assert(p.count==n);
    petting_t sweep={0};for(unsigned t=1000;t<=4000;t+=16)petting_update(&sweep,true,70+(t-1000)*.1f,70,t);
    assert(sweep.count==1); /* one long swipe cannot impersonate repeated strokes */
    puts("PASS continuous forehead caresses, stationary jitter and wrong-region rejection");
}
static void sickness_test(void)
{
    unsigned games=0,pucks=0,reels=0,jelly=0,sick=0,cross=0;uint32_t shortest=20000,longest=0;
    for(unsigned seed=1;seed<500;seed++) {
        behavior_t b;behavior_init(&b,1000);b.rng=seed*7919;b.energy=.9f;b.shake_time_ms=1000;b.shake=.5f;
        b.axis_motion[1]=seed%2?1:0;b.axis_motion[0]=seed%2?0:1;
        behavior_in_t in={.idle_allowed=true};behavior_out_t out;behavior_update(&b,&in,1100,&out);
        if(b.state==BEH_WOBBLE_GAME){games++;pucks+=b.game_anim==ANIM_PUCKS;reels+=b.game_anim==ANIM_HIGH_ROLLER;jelly+=b.game_anim==ANIM_JELLY;
            assert(b.game_len_ms>=5000&&b.game_len_ms<=10000);b.shake_time_ms=0;b.shake=0;
            behavior_update(&b,&in,1100+b.game_len_ms,&out);assert(b.state==BEH_DIZZY);
        }
        assert(b.state==BEH_DIZZY);sick+=b.dizzy_anim==ANIM_SEASICK;cross+=b.dizzy_anim==ANIM_CROSS_EYED;
        if(b.dizzy_len_ms<shortest)shortest=b.dizzy_len_ms;if(b.dizzy_len_ms>longest)longest=b.dizzy_len_ms;
        uint32_t end=b.state_since_ms+b.dizzy_len_ms;b.shake_time_ms=0;b.shake=0;
        behavior_update(&b,&in,end-1,&out);assert(b.state==BEH_DIZZY);
        behavior_update(&b,&in,end,&out);assert(b.state==BEH_IDLE);
        b.state=BEH_WOBBLE_GAME;b.shake_time_ms=4000;b.shake=.5f;
        behavior_update(&b,&in,end+8,&out);assert(b.state!=BEH_KNOCKED_OUT);
        b.shake_time_ms=8000;
        behavior_update(&b,&in,end+16,&out);assert(b.state==BEH_KNOCKED_OUT);
    }
    assert(games>100&&pucks&&reels&&jelly&&sick&&cross&&longest-shortest>3000);
    /* The meter accumulates from real calibrated input and slowly recovers. */
    behavior_t b;behavior_init(&b,1000);imu_cal_t cal;imu_cal_default(&cal);
    behavior_in_t in={.idle_allowed=true,.have_accel=true,.cal=&cal};behavior_out_t out;
    for(unsigned t=1016;t<5000;t+=16){in.accel_ms=t;in.accel[2]=(int16_t)(4096*(1+.6f*sinf(t*.04f)));behavior_update(&b,&in,t,&out);}
    assert(b.sickness>.15f);float before=b.sickness;
    for(unsigned t=5000;t<12000;t+=16){in.accel_ms=t;in.accel[2]=4096;behavior_update(&b,&in,t,&out);}
    assert(b.sickness<before);in.dancing=true;behavior_update(&b,&in,12016,&out);assert(b.sickness==0);
    printf("PASS sickness meter, %u random games (%u puck/%u reel/%u jelly), delayed varied dizzy %u..%u ms, KO preemption\n",games,pucks,reels,jelly,shortest,longest);
}
static void physics_test(void)
{
    unsigned hits=0;
    for(unsigned seed=1;seed<80;seed++) {
        pucks_t p;pucks_init(&p,1000,seed);
        for(unsigned t=1016;t<11000;t+=16) {
            pucks_update(&p,t,(.15f+seed*.025f)*sinf(t*(.0008f+seed*.00002f)),cosf(t*.0017f));
            for(int i=0;i<2;i++)assert(hypotf(fabsf(p.x[i]-233)+p.hw[i]-p.radius[i],fabsf(p.y[i]-233)+p.hh[i]-p.radius[i])+p.radius[i]<230.1f);
            float r=p.radius[0]+p.radius[1];
            assert(hypotf(fmaxf(0,fabsf(p.x[1]-p.x[0])-(p.hw[0]+p.hw[1]-r)),fmaxf(0,fabsf(p.y[1]-p.y[0])-(p.hh[0]+p.hh[1]-r)))>=r-1.f);
        }
        hits+=p.hits;
    }
    assert(hits>500);
    pucks_t still,left,right,hard;pucks_init(&still,1000,1);
    left=right=hard=still;
    for(unsigned t=1016;t<1090;t+=16){pucks_update(&still,t,.01,-.01);pucks_update(&left,t,-.25,0);pucks_update(&right,t,.25,0);pucks_update(&hard,t,1,0);}
    assert(still.x[0]==138&&still.y[0]==233&&!still.hits);
    assert(left.x[0]<138&&right.x[0]>138&&hard.x[0]>right.x[0]+10);
    float speed=hypotf(hard.vx[0],hard.vy[0]);
    for(unsigned t=1090;t<10000;t+=16)pucks_update(&hard,t,0,0);
    assert(hypotf(hard.vx[0],hard.vy[0])<speed*.02f);

    pucks_t settled;pucks_init(&settled,1000,1);
    for(unsigned t=1016;t<12000;t+=16)pucks_update(&settled,t,0,1);
    unsigned contacts=settled.hits;
    for(unsigned t=12000;t<16000;t+=16)assert(pucks_update(&settled,t,0,1)==0);
    assert(settled.hits==contacts);
    clock_t begin=clock();
    for(unsigned t=16000;t<320000;t+=16)pucks_update(&settled,t,sinf(t*.017f),cosf(t*.011f));
    printf("Loose-eye physics: %.2f host us/frame (2 bodies, bounded substeps)\n",(clock()-begin)*1e6/CLOCKS_PER_SEC/19000);
    anim_sm_t sm;eyes_init(&eyes,1000);anim_init(&sm,&eyes,1000);anim_set(&sm,&eyes,ANIM_PUCKS,1000);
    for(unsigned t=1000;t<11000;t+=16){anim_update(&sm,&eyes,t);eyes_update(&eyes,t,shapes);
        for(int i=0;i<2;i++){assert(shapes[i].visible&&eyes.eye[i].motion_k==0);if(t>=1400){assert(shapes[i].hw>Q16(35)&&shapes[i].hh>Q16(45));assert(shapes[i].rad[0]<shapes[i].hw*95/100);}}}
    printf("PASS %u bounded puck collisions, no elastic distortion, recognizable rounded eyes\n",hits);
}
static void headbutt_test(void)
{
    behavior_t b;behavior_init(&b,1000);behavior_in_t in={.idle_allowed=true};behavior_out_t out;unsigned t=1100;
    float previous=b.valence;
    for(int stage=1;stage<=4;stage++) {
        for(int tap=0;tap<4;tap++,t+=200){in.tap_count++;behavior_update(&b,&in,t,&out);}
        assert(b.state==BEH_HEADBUTT&&out.override_anim==ANIM_HEADBUTT&&b.headbutt_stage==stage);
        assert(b.valence<=previous);if(previous>-.999f)assert(b.valence<previous);previous=b.valence;
        t=b.state_since_ms+600;behavior_update(&b,&in,t,&out);assert(b.crack_stage==stage);
        in.tap_count++;behavior_update(&b,&in,t+100,&out);assert(b.headbutt_stage==stage); /* no extra stage mid-slam */
        t=b.state_since_ms+3000;behavior_update(&b,&in,t,&out);assert(b.state==BEH_IDLE);t+=100;
        memset(full,0,sizeof full);memset(tile,0,sizeof tile);
        gfx_band_t gb={full,0,0,466,466};cracks_paint(&gb,stage);
        for(int y=0;y<466;y+=7){uint16_t row[466*7]={0};gfx_band_t band={row,0,y,466,466-y<7?466-y:7};cracks_paint(&band,stage);memcpy(tile+y*466,row,band.rows*466*2);}
        assert(!memcmp(full,tile,sizeof full));
    }
    anim_sm_t sm;eyes_init(&eyes,1000);anim_init(&sm,&eyes,1000);anim_set(&sm,&eyes,ANIM_HEADBUTT,1000);
    unsigned bonks=0,serial=sm.effect_serial;
    for(unsigned now=1000;now<4000;now+=16){anim_update(&sm,&eyes,now);if(sm.effect_serial!=serial){serial=sm.effect_serial;assert(sm.effect==SFX_BONK);assert(now>=1500&&now<1533);bonks++;}}
    assert(bonks==1); /* one real effect event at the visible impact */
    t=b.crack_ms+6600;behavior_update(&b,&in,t,&out);assert(!b.crack_stage&&!b.headbutt_stage);
    behavior_init(&b,1000);in.dancing=true;
    for(t=1100;t<2500;t+=200){in.tap_count++;behavior_update(&b,&in,t,&out);}assert(!b.headbutt_stage);
    puts("PASS four deliberate idle tap bursts, escalating anger/fracture, no stage skip, repair and dance immunity; crack tile equality");
}
static void sound_test(void)
{
    sfx_t s={0};int16_t pcm[256];
    for(int id=SFX_PUCK;id<=SFX_TINKLE;id++) {
        sfx_start(&s,id,1);unsigned power=0;
        for(int f=0;f<80;f++){memset(pcm,0,sizeof pcm);sfx_mix(&s,pcm,256);for(int i=0;i<256;i++)power+=pcm[i]<0?-pcm[i]:pcm[i];}
        assert(power>10000&&!sfx_active(&s));
    }
    clock_t begin=clock();
    for(int f=0;f<2000;f++){sfx_start(&s,SFX_PUCK,1);sfx_start(&s,SFX_REEL,1);sfx_start(&s,SFX_GLASS,1);memset(pcm,0,sizeof pcm);sfx_mix(&s,pcm,256);}
    printf("SFX 3-voice mix: %.2f host us per 16 ms frame\n",(clock()-begin)*1e6/CLOCKS_PER_SEC/2000);
    puts("PASS bounded procedural collision/reel/impact/glass sounds, no lingering voice");
}
int main(void){pet_test();sickness_test();physics_test();headbutt_test();sound_test();return 0;}
