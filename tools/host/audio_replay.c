/* Replays 16 kHz mono PCM WAV through the exact firmware analyser. */
#include <stdio.h>
#include <time.h>
#define AUDIO_ANALYSIS_HOST 1
#include "../../main/audio.c"
#include "behavior.h"
static unsigned u32(const unsigned char *p){return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24;}
int main(int argc,char **argv)
{
 if(argc<2){fprintf(stderr,"usage: audio_replay input.wav [target RMS, default 300; 0 preserves input level] [frames.csv]\n");return 1;}
 FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
 unsigned char h[44];if(fread(h,1,12,f)!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4))return 2;
 unsigned bytes=0;int valid=0;
 while(fread(h,1,8,f)==8){unsigned n=u32(h+4);if(!memcmp(h,"fmt ",4)){
  if(n<16||fread(h+8,1,16,f)!=16)return 2;
  valid=h[8]==1&&h[10]==1&&u32(h+12)==16000&&h[22]==16;fseek(f,(long)n-16+(n&1),SEEK_CUR);
 }else if(!memcmp(h,"data",4)){bytes=n;break;}else fseek(f,n+(n&1),SEEK_CUR);}
 if(!valid||bytes<FRAME*2||(bytes&1))return 2;
 int16_t *pcm=malloc(bytes);if(!pcm||fread(pcm,1,bytes,f)!=bytes)return 2;fclose(f);
 unsigned samples=bytes/2;double sum=0;for(unsigned i=0;i<samples;i++)sum+=(double)pcm[i]*pcm[i];
 float target=argc>2?atof(argv[2]):300;
 float gain=target>0 && sum>0 ? target/sqrt(sum/samples) : 1.f;
 FILE *csv=argc>3?fopen(argv[3],"w"):NULL;
 if(argc>3&&!csv){perror(argv[3]);free(pcm);return 1;}
 if(csv)fprintf(csv,"time_ms,beat,kick,raw_loud,bpm,confidence,ratio,speech,kick_raw,bass_raw,mid_raw,high_raw\n");
 tables_init();analysis_reset();clock_t cpu=0;unsigned admitted=0,frames=0,danced=0,listened=0;uint32_t prev=0;
 behavior_t beh;behavior_init(&beh,1000);beh.energy=1;
 for(unsigned pos=0;pos+FRAME<=samples;pos+=FRAME){
  int16_t stereo[FRAME*2];for(int i=0;i<FRAME;i++){float v=pcm[pos+i]*gain;if(v>32767)v=32767;if(v< -32768)v=-32768;stereo[2*i]=stereo[2*i+1]=(int16_t)v;}
  uint32_t now=1000+pos/16;clock_t t=clock();analyse(stereo,now);cpu+=clock()-t;frames++;
  behavior_in_t in={.mic_available=true,.usb=true,.audio=s_feat};behavior_out_t out;
  behavior_update(&beh,&in,now,&out);
  danced+=beh.state==BEH_MUSIC;listened+=beh.state==BEH_LISTENING;
  int beat=s_feat.beat_count!=prev;prev=s_feat.beat_count;
  if(s_feat.beat_count>=8&&s_feat.bpm>=85&&s_feat.bpm<=185&&s_feat.tempo_conf>=.75&&s_feat.bass_ratio>=.08)admitted++;
  if(csv)fprintf(csv,"%u,%d,%.5f,%.2f,%.2f,%.3f,%.3f,%d,%.6f,%.6f,%.6f,%.6f\n",pos/16,beat,s_feat.kick,s_feat.raw_loud,s_feat.bpm,s_feat.tempo_conf,s_feat.bass_ratio,s_feat.speech,s_feat.kick*s_max_kick/fmaxf(.001f,s_presence),s_feat.bass*s_max_bass/fmaxf(.001f,s_presence),s_feat.mid*s_max_mid/fmaxf(.001f,s_presence),s_feat.high*s_max_high/fmaxf(.001f,s_presence));
 }
 if(csv)fclose(csv);
 printf("%s: frames=%u beats=%u bpm=%.1f conf=%.2f eligible=%u (%.1f%%) CPU=%.2f us/frame\n",argv[1],frames,s_feat.beat_count,s_feat.bpm,s_feat.tempo_conf,admitted,100.f*admitted/frames,(double)cpu*1e6/CLOCKS_PER_SEC/frames);
 printf("behavior: danced=%u frames, listened=%u frames (awake, full energy)\n",danced,listened);
 free(pcm);return 0;
}
