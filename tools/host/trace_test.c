#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "music_trace.h"
int main(void){
 uint8_t p[24];audio_features_t a={.raw_loud=300,.bpm=150,.bass_ratio=.4,.tempo_conf=1,.speech_depth=.5,.bass=.6,.mid=.3,.high=.2};
 music_trace_pack(p,&a,123456,.003f,.002f,.001f,1,3,7);
 assert(p[0]==64&&p[1]==226&&p[2]==1&&p[3]==0);assert(p[4]==44&&p[5]==1);assert(p[14]==255&&p[16]==255&&p[22]==7&&p[23]==0);
 FILE *f=fopen("tools/host/out/trace-fixture.log","w");assert(f);fputs("I (123) music_trace: MC1:",f);for(int i=0;i<24;i++)fprintf(f,"%02x",p[i]);fputc('\n',f);fclose(f);
 music_trace_pack(p,&a,0,1,1,1,-1,255,65535);assert(p[6]==255&&p[7]==255&&p[14]==0&&p[22]==255&&p[23]==255);
 puts("PASS: explicit 24-byte encoding, saturation, session IDs; fixture written");
}
