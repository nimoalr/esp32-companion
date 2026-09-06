#include <assert.h>
#include <stdio.h>
#include <time.h>
#define AUDIO_ANALYSIS_HOST 1
#include "../../main/audio.c"
#include "behavior.h"
static uint32_t rng=17;
static float noise(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (float)(rng&65535)/32768.f-1.f;}
static void trial(const char *name,float bpm,int mode,bool muted)
{
 tables_init();analysis_reset();s_muted=muted;rng=17;
 behavior_t beh;behavior_init(&beh,1000);beh.energy=1;
 unsigned danced=0,listened=0;
 unsigned eligible=0,run=0,longest=0;clock_t cpu=0;float phase=0;uint32_t steps=2500;
 for(uint32_t f=0;f<steps;f++){
  int16_t pcm[512];
  for(int i=0;i<256;i++){
   float t=(f*256+i)/16000.f,p=fmodf(t,60.f/bpm),v=0;
   if(mode==0){
    /* pitched kick with a sharp body, loud offbeat snare, hats and rolling bass */
    float kick=.045f*expf(-p*24.f)*sinf(6.2831853f*(55.f*p+1.5f*(1.f-expf(-p*55.f))));
    float sp=fmodf(p+30.f/bpm,60.f/bpm);
    float snare=.055f*expf(-sp*55.f)*noise();
    float roll=.009f*sinf(t*6.2831853f*120.f)*(0.2f+0.8f*fabsf(sinf(t*6.2831853f*bpm/30.f)));
    v=kick+snare+roll+.002f*noise();
   } else if(mode==1) {
    /* Irregular voiced syllables with harmonics and occasional plosive bursts. */
    float rate=3.8f+1.4f*sinf(t*1.71f)+.7f*sinf(t*3.21f);phase+=rate/16000.f;
    float env=fmaxf(0.f,sinf(phase*6.2831853f));env*=env;
    float fundamental=115.f+12.f*sinf(t*2.3f);
    v=.02f*env*(sinf(t*6.2831853f*fundamental)+.6f*sinf(t*6.2831853f*fundamental*2)+.3f*noise());
   } else if(mode==2) v=.0008f*noise();
   else v=.02f*sinf(t*6.2831853f*60.f);
   if(v>1)v=1;if(v< -1)v=-1;pcm[2*i]=pcm[2*i+1]=(int16_t)(v*32767);
  }
  clock_t t0=clock();analyse(pcm,1000+f*16);cpu+=clock()-t0;
  behavior_in_t in={.mic_available=true,.usb=true,.audio=s_feat};behavior_out_t out;
  behavior_update(&beh,&in,1000+f*16,&out);
  if(beh.state==BEH_MUSIC)danced++;
  if(beh.state==BEH_LISTENING)listened++;
  if(s_feat.beat_count>=8&&s_feat.bpm>=85&&s_feat.bpm<=185&&s_feat.tempo_conf>=.75&&s_feat.bass_ratio>=.08){eligible++;run++;if(run>longest)longest=run;}else run=0;
 }
 printf("%s: beats=%u BPM=%.1f confidence=%.2f music frames=%u/%u CPU=%.2f us/frame\n",name,s_feat.beat_count,s_feat.bpm,s_feat.tempo_conf,eligible,steps,(double)cpu*1e6/CLOCKS_PER_SEC/steps);
 printf("longest candidate %u ms, danced %u, listened %u\n",longest*16,danced,listened);
 if(muted){assert(s_feat.beat_count==0&&eligible==0);}
 else if(mode==0){assert(eligible>steps/2);assert(danced>steps/2);assert(fabsf(s_feat.bpm-bpm)<bpm*.08f);assert(s_feat.beat_count>40*bpm/60-8);}
 else assert(danced==0);
 s_muted=false;
}
static void coast_test(void)
{
 behavior_t b;behavior_init(&b,1000);b.state=BEH_MUSIC;b.music_quiet_since_ms=1000;
 behavior_in_t in={.mic_available=true,.usb=true,.audio={.active=true,.raw_loud=200,.bass_ratio=.3f,.beat_count=10,.bpm=150,.tempo_conf=1}};
 behavior_out_t out;
 for(uint32_t t=1000;t<42000;t+=16){
  if(t<3000||t>39000){in.audio.last_beat_ms=t-t%400;in.audio.tempo_conf=1;}
  else in.audio.tempo_conf=0;
  behavior_update(&b,&in,t,&out);assert(b.state==BEH_MUSIC&&out.override_anim==ANIM_DANCE);
 }
 in.audio.raw_loud=0;in.audio.tempo_conf=0;
 for(uint32_t t=42000;t<44500;t+=16)behavior_update(&b,&in,t,&out);
 assert(b.state!=BEH_MUSIC);
 behavior_init(&b,1000);b.state=BEH_MUSIC;b.music_quiet_since_ms=1000;in.audio.raw_loud=200;
 for(uint32_t t=1000;t<46500;t+=16)behavior_update(&b,&in,t,&out);
 assert(b.state!=BEH_MUSIC); /* endless unrelated sound cannot hold it forever */
 puts("PASS: audible 36 s breakdown retained; beat return immediate; silence exits; unconfirmed sound grace bounded");
}
int main(void){setvbuf(stdout,NULL,_IONBF,0);trial("EDM 100",100,0,false);trial("EDM 125",125,0,false);trial("EDM 150",150,0,false);trial("EDM 180",180,0,false);trial("speech-like",150,1,false);trial("room noise",150,2,false);trial("mains hum",150,3,false);trial("own playback",150,0,true);coast_test();return 0;}
