#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "music_trace.h"
int main(void){
 uint8_t p[64];float power[16];for(int i=0;i<16;i++)power[i]=(i+1)*.001f;audio_features_t a={.raw_loud=300,.bpm=150,.bass_ratio=.4,.tempo_conf=1,.speech_depth=.5,.bass=.6,.mid=.3,.high=.2};
 music_trace_pack(p,&a,123456,.003f,.002f,.001f,1,3,7,power,42,777);
 assert(p[0]==64&&p[1]==226&&p[2]==1&&p[3]==0);assert(p[4]==44&&p[5]==1);assert(p[14]==255&&p[16]==255&&p[22]==7&&p[23]==0);
 FILE *f=fopen("tools/host/out/trace-fixture.log","w");assert(f);fputs("I (123) music_trace: MC1:",f);for(int i=0;i<24;i++)fprintf(f,"%02x",p[i]);fputc('\n',f);fclose(f);
 char line[MUSIC_TRACE_LINE_BYTES];size_t size=music_trace_line(line,p,1);
 f=fopen("tools/host/out/trace-v2-fixture.log","w");assert(f);assert(fwrite(line,1,size,f)==size);fclose(f);
 assert(p[24]==42&&p[28]==9&&p[29]==3);assert(!music_trace_line(line,p,9));
 music_trace_pack(p,&a,0,1,1,1,-1,255,65535,power,0xffffffff,100000);assert(p[6]==255&&p[7]==255&&p[14]==0&&p[22]==255&&p[23]==255);
 puts("PASS: v1 prefix, 64-byte raw-spectrum encoding, CRC framing, saturation, sequence IDs; fixtures written");
}
