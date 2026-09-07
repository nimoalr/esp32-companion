#include <assert.h>
#include <stdio.h>
#include <math.h>
#define AUDIO_ANALYSIS_HOST 1
#include "../../main/audio.c"
static void stream(bool speech,bool muted)
{
    analysis_reset();s_muted=muted;tables_init();float ph=0;unsigned first=0;
    for(unsigned f=0;f<2000;f++){
        int16_t pcm[512];
        for(int i=0;i<256;i++){
            float t=(f*256+i)/16000.f;
            float gap=t<12?.48f:t<18?.24f:t<24?.48f:.32f;
            ph+=1.f/16000.f;if(ph>=gap)ph-=gap;
            float v=.035f*expf(-ph*50)*sinf(6.2831853f*60*ph);
            if(speech) v=.025f*powf(fmaxf(0,sinf(t*(21+2*sinf(t*1.1f)))),2)*sinf(t*6.2831853f*140);
            pcm[i*2]=pcm[i*2+1]=(int16_t)(v*32767);
        }
        analyse(pcm,1000+f*16);
        if(s_feat.rush_count&&!first)first=f*16;
    }
    printf("rush PCM speech=%d muted=%d events=%u first=%u bpm=%.1f\n",speech,muted,s_feat.rush_count,first,s_feat.rush_bpm);
    if(speech||muted)assert(!s_feat.rush_count);
    else {assert(first>=12000&&first<14500);assert(s_feat.rush_count>=2&&s_feat.rush_count<=3);}
}
int main(void)
{
    rhythm_rush_t r={0};
    for(unsigned t=1000;t<11000;t+=16)rhythm_rush_update(&r,t,t%480<16,480,.4,false);
    assert(!r.count);
    for(unsigned t=11000;t<12500;t+=16)rhythm_rush_update(&r,t,t%240<16,480,.4,false);
    assert(r.count==1&&r.bpm>240&&r.bpm<260);
    for(unsigned t=12500;t<18000;t+=16)rhythm_rush_update(&r,t,t%240<16,480,.4,false);
    assert(r.count==1);
    r=(rhythm_rush_t){0};for(unsigned t=1000;t<5000;t+=128)rhythm_rush_update(&r,t,true,0,.4,false);assert(!r.count);
    r=(rhythm_rush_t){0};for(unsigned t=1000;t<5000;t+=128)rhythm_rush_update(&r,t,true,480,.01,false);assert(!r.count);
    r=(rhythm_rush_t){0};for(unsigned t=1000;t<5000;t+=128)rhythm_rush_update(&r,t,true,480,.4,true);assert(!r.count);
    r=(rhythm_rush_t){0};rhythm_rush_update(&r,1000,true,480,.4,false);
    for(unsigned t=10000;t<14000;t+=128)rhythm_rush_update(&r,t,true,0,.4,false);assert(!r.count);
    stream(false,false);stream(true,false);stream(false,true);puts("PASS: fast subdivisions, cooldown, unconfirmed/quiet/muted/stale rejection");
}
