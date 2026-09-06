/* Personality scene admission, deliberate petting, sensor motion and eye pokes. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "persona.h"
uint32_t esp_random(void) { return 17; }
static eyes_t eyes;
static anim_sm_t anim;
static raster_shape_t shapes[2];
static void cameos(void)
{
    bool seen[ANIM_COUNT]={0};
    behavior_t b;behavior_init(&b,1000);
    behavior_in_t in={.idle_allowed=true};behavior_out_t out;
    uint32_t t=1000;
    for(int e=0;e<3;e++)for(int v=0;v<3;v++)for(int n=0;n<300;n++,t+=16) {
        b.energy=.2f+.3f*e;b.valence=(v-1)*.6f;b.mood_tick_ms=t;
        b.idle_action=-1;b.next_action_ms=t;
        behavior_update(&b,&in,t,&out);
        assert(out.override_anim>=ANIM_SMUG);
        seen[out.override_anim]=true;
    }
    for(int a=ANIM_SMUG;a<ANIM_COUNT;a++)if(a!=ANIM_KNOCKED_OUT&&a!=ANIM_RECOVERING)assert(seen[a]);
    behavior_init(&b,1000);b.idle_action=ANIM_HIDE_RELOCATE;b.action_started_ms=1000;
    eyes_init(&eyes,1000);anim_init(&anim,&eyes,1000);anim_set(&anim,&eyes,ANIM_HIDE_RELOCATE,1000);
    for(t=1000;t<=9016;t+=16) {
        in.shown_anim=anim.id;in.shown_anim_done=t-anim.t_change_ms>=anim_action_ms(anim.id);
        behavior_update(&b,&in,t,&out);
        if(t<9000)assert(out.override_anim==ANIM_HIDE_RELOCATE);
        else assert(out.override_anim!=ANIM_HIDE_RELOCATE);
        anim_update(&anim,&eyes,t);
    }
    b.idle_action=ANIM_HIDE_RELOCATE;b.action_started_ms=9100;in.user_interacting=true;
    behavior_update(&b,&in,9200,&out);assert(b.idle_action<0&&out.override_anim==b.idle_anim);
    in.user_interacting=false;in.audio.active=in.audio.speech=true;b.next_action_ms=9200;
    behavior_update(&b,&in,9300,&out);assert(b.state==BEH_LISTENING&&b.idle_action<0);
    in.audio.speech=false;b.state=BEH_MUSIC;b.music_quiet_since_ms=9400;in.audio.raw_loud=200;
    behavior_update(&b,&in,9400,&out);assert(out.override_anim==ANIM_DANCE&&b.idle_action<0);
    behavior_init(&b,1000);memset(&in,0,sizeof in);b.next_action_ms=1000;
    behavior_update(&b,&in,1200,&out);assert(b.idle_action<0); /* asleep/inactive renderer */
    in.idle_allowed=true;in.dancing=true;b.next_action_ms=1200;
    behavior_update(&b,&in,1300,&out);assert(b.idle_action<0&&out.override_anim<0); /* preserve manual dance */
    puts("PASS: all 26 cameos reachable by mood, finish once, touch/speech/music preempt");
}
static void purring(void)
{
    persona_t p;persona_in_t in={.power=0,.beh=BEH_IDLE,.anim=ANIM_NEUTRAL,.chattiness=0,.batt_pct=80};
    persona_say_t out;persona_init(&p,1000,17);persona_tick(&p,&in,1000,&out);
    in.beh=BEH_CARRIED;in.finger=true;in.handling=true;
    for(uint32_t t=1016;t<12000;t+=16) {
        persona_tick(&p,&in,t,&out);assert(!(out.kind==SAY_GESTURE&&out.id==VOICE_PURR));
    }
    in.beh=BEH_IDLE;in.handling=false;
    unsigned purrs=0;
    for(uint32_t t=12000;t<19000;t+=16) {
        persona_tick(&p,&in,t,&out);
        if(out.kind==SAY_GESTURE&&out.id==VOICE_PURR){assert(t>15500);purrs++;}
    }
    assert(purrs==1);
    /* Movement restarts an in-progress hold, even while other speech is busy. */
    in.handling=true;persona_tick(&p,&in,20000,&out);assert(p.finger_since_ms==0);
    in.handling=false;in.speaking=true;persona_tick(&p,&in,21000,&out);
    assert(p.finger_since_ms==21000);
    in.handling=true;persona_tick(&p,&in,23000,&out);assert(p.finger_since_ms==0);
    behavior_t b;behavior_init(&b,1000);behavior_in_t bi={.stroke_forehead=true};behavior_out_t bo;
    bi.stroke_count=1;behavior_update(&b,&bi,1100,&bo);assert(b.state!=BEH_PETTED);
    bi.stroke_count=2;behavior_update(&b,&bi,1900,&bo);assert(b.state==BEH_PETTED);
    behavior_init(&b,1000);bi.stroke_count=1;behavior_update(&b,&bi,1100,&bo);
    bi.stroke_count=2;behavior_update(&b,&bi,5000,&bo);assert(b.state!=BEH_PETTED);
    assert(!strcmp(behavior_state_name(BEH_CARRIED),"carried"));
    assert(!strcmp(behavior_state_name(BEH_PETTED),"petted"));
    puts("PASS: carrying/grips cannot purr; settled hold once, motion resets it; two deliberate strokes");
}
static void handling_and_lids(void)
{
    behavior_t b;behavior_init(&b,1000);imu_cal_t cal;imu_cal_default(&cal);
    behavior_in_t in={.cal=&cal,.have_accel=true};behavior_out_t out;
    for(uint32_t t=1016;t<7000;t+=16) {
        const float angle=(t-1000)*.0007f;
        in.accel[0]=(int16_t)(4096.f*sinf(angle));in.accel[1]=(int16_t)(-4096.f*cosf(angle));in.accel[2]=0;in.accel_ms=t;
        behavior_update(&b,&in,t,&out);
        assert(b.shake<.001f); /* pure rotation has no translational shake */
    }
    /* Fixed gravity and mild magnitude variation must not produce a 6 Hz wobble. */
    b.gx=b.gy=b.face_angle=b.face_target=0;b.gz=1;b.state=BEH_IDLE;b.shake=.10f;
    behavior_update(&b,&in,7016,&out);assert(out.env[0].dx==0);
    b.shake=.36f;behavior_update(&b,&in,7050,&out);assert(out.env[0].dx!=0);
    eyes_init(&eyes,1000);eyes_set_idle_rates(&eyes,0,Q16_ONE,0);
    eye_pose_t pose=EYE_POSE_NEUTRAL;
    pose.lid_top=Q16(.15);pose.lid_bottom=Q16(.3);pose.curve=Q16(.2);
    for(int e=0;e<2;e++)eyes_set_target(&eyes,e,&pose,0,1000);
    eyes_update(&eyes,1016,shapes);
    const int32_t right_bottom=shapes[1].bot_base,right_curve=shapes[1].curve;
    for(int frame=0;frame<=100;frame++) {
        eye_pose_t env={.lid_top=Q16(.75)*frame/100,.dx=(frame&1)?Q16(2):Q16(-2)};
        eyes_set_env(&eyes,0,&env);eyes_update(&eyes,1032+frame*16,shapes);
        assert(shapes[0].top_base<=shapes[0].bot_base-shapes[0].curve);
        assert(shapes[1].bot_base==right_bottom&&shapes[1].curve==right_curve);
        assert(eyes.eye[0].motion_k==0); /* sensor offsets do not trigger elastic stretch */
    }
    assert(shapes[0].curve==0&&shapes[0].bot_base==shapes[0].cy+shapes[0].hh);
    eye_pose_t clear={0};eyes_set_env(&eyes,0,&clear);eyes_update(&eyes,3000,shapes);
    assert(shapes[0].curve>0);
    /* Eye pokes work even if the IMU is unavailable. */
    behavior_init(&b,1000);memset(&in,0,sizeof in);in.tap_count=1;in.poke_eye=1;
    behavior_update(&b,&in,1100,&out);behavior_update(&b,&in,1400,&out);
    assert(out.env[0].lid_top>0&&out.env[1].lid_top==0);
    puts("PASS: gentle rotation quiet, strong shake wobbles; tapped lower lid relaxes/restores, other eye unchanged");
}
int main(void){cameos();purring();handling_and_lids();return 0;}
