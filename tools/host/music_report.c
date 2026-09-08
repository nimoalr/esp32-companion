/* Export compact Music Lab sessions to CSV; preserve labels as JSON. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const unsigned char *p){return u16(p)|(uint32_t)u16(p+2)<<16;}
int main(int argc,char **argv){
 if(argc!=4){fprintf(stderr,"usage: music_report session.mcal frames.csv labels.json\n");return 1;}
 FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
 unsigned char p[1092];if(fread(p,1,12,f)!=12||(memcmp(p,"MCALv001",8)&&(memcmp(p,"MCALv002",8)&&memcmp(p,"MCALv003",8)))){fprintf(stderr,"invalid header\n");return 2;}
 unsigned bytes=p[7]=='3'?1092:p[7]=='2'?64:24;
 unsigned n=u32(p+8);if(n>1000000){fprintf(stderr,"metadata too large\n");return 2;}
 char *meta=malloc(n+1);if(!meta||fread(meta,1,n,f)!=n){fprintf(stderr,"truncated metadata\n");return 2;}meta[n]=0;
 FILE *json=fopen(argv[3],"wb"),*csv=fopen(argv[2],"w");if(!json||!csv){perror("output");return 1;}
 fwrite(meta,1,n,json);int error=ferror(json);error|=fclose(json)!=0;free(meta);
 fprintf(csv,"frame,device_ms,session,raw_rms,kick_rms,mean_rms,previous_rms,bpm,presence,bass_ratio,confidence,speech_depth,candidate,beat,speech,own_voice,clipped,rush,dancing,listening,bass,mid,high,sequence,analysis_us,peak");
 for(int i=0;i<16;i++)fprintf(csv,",raw_fft_power_%d",i);fputc('\n',csv);
 unsigned frames=0,beats=0,rush=0;size_t got;
 while((got=fread(p,1,bytes,f))==bytes){unsigned flags=p[18];beats+=!!(flags&2);rush+=!!(flags&32);
 fprintf(csv,"%u,%u,%u,%u,%.2f,%.2f,%.2f,%.1f,%.4f,%.4f,%.4f,%.4f",frames++,u32(p),u16(p+22),u16(p+4),u16(p+6)/4.,u16(p+8)/4.,u16(p+10)/4.,u16(p+12)/10.,p[14]/255.,p[15]/127.5,p[16]/255.,p[17]/64.);
 for(int b=0;b<8;b++)fprintf(csv,",%u",!!(flags&(1u<<b)));fprintf(csv,",%.4f,%.4f,%.4f",p[19]/255.,p[20]/255.,p[21]/255.);
 if(bytes>=64&&u16(p+30)!=65535){fprintf(csv,",%u,%u,%u",u32(p+24),u16(p+28),u16(p+30));for(int i=0;i<16;i++){uint32_t bits=(uint32_t)u16(p+32+2*i)<<16;float power;memcpy(&power,&bits,4);fprintf(csv,",%.9g",power);}}
 else for(int i=0;i<19;i++)fputc(',',csv);
 fputc('\n',csv);
 }
 error|=got!=0||ferror(f)||ferror(csv);error|=fclose(f)!=0;error|=fclose(csv)!=0;
 if(error){fprintf(stderr,"truncated input or output error\n");return 2;}
 printf("%u frames, %u bytes of records, %u accepted beats, %u rush events; run boundaries/markers in %s\n",frames,frames*bytes,beats,rush,argv[3]);return 0;
}
