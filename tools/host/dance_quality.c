/* Compare normal four-row AA with the busy-dance two-row setting. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eyes.h"
#include "dance_lasers.h"
uint32_t esp_random(void){return 17;}
static eyes_t eye;static raster_shape_t shapes[2];static dance_lasers_t lights;
static uint16_t frames[2][466*466],tile[466*466];
int main(void){
 eyes_init(&eye,0);eyes_set_idle_rates(&eye,0,Q16_ONE,0);eyes_set_hotspot(&eye,true);
 eyes_set_face_angle(&eye,33);eyes_set_fx(&eye,RASTER_FX_BARS,1);
 const float bars[8]={.4,.8,.3,.7,.5,.9,.2,.6};
 for(int i=0;i<2;i++)eyes_set_bar_heights(&eye,i,bars);
 eyes_update(&eye,16,shapes);
 audio_features_t a={.loud=.8f,.bass=.7f,.kick=.8f,.beat_count=17,.last_beat_ms=8000};
 dance_background_update(&lights,1,1,&a,8000,33);
 for(int mode=0;mode<2;mode++){
  for(int i=0;i<2;i++)shapes[i].aa_samples=mode?2:4;
  dance_lasers_paint(&lights,frames[mode],0,0,466,466,shapes);
  for(int y=0;y<466;y+=7)dance_lasers_paint(&lights,tile+y*466,0,y,466,466-y<7?466-y:7,shapes);
  assert(!memcmp(tile,frames[mode],sizeof tile));
 }
 eyes_update(&eye,32,shapes);
 assert(!shapes[0].aa_samples && !shapes[1].aa_samples); /* no quality leakage after dance */
 unsigned changed=0;for(int i=0;i<466*466;i++)changed+=frames[0][i]!=frames[1][i];
 assert(changed<466*466/100); /* Quality difference stays confined to a small edge footprint. */
 FILE *f=fopen("tools/host/out/dance-quality.ppm","wb");assert(f);fprintf(f,"P6\n932 466\n255\n");
 for(int y=0;y<466;y++)for(int mode=0;mode<2;mode++)for(int x=0;x<466;x++){
  unsigned c=frames[mode][y*466+x];c=(c>>8|c<<8)&65535;
  unsigned char rgb[3]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(rgb,1,3,f);
 }
 assert(!fclose(f));printf("PASS: 4/2-sample tile equality; only %u / 217156 pixels differ (preview: left 4, right 2)\n",changed);
}
