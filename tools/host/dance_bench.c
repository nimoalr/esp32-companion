#include <stdio.h>
#include <time.h>
#include "anim.h"
#include "dance_lasers.h"
uint32_t esp_random(void) { return 1; }
static eyes_t eyes; static anim_sm_t sm; static raster_shape_t sh[2];
static uint16_t band[466*32];
int main(void) {
 for (int rot=0;rot<2;rot++) for (int fx=0;fx<=3;fx++) {
  eyes_init(&eyes,0);anim_init(&sm,&eyes,0);eyes_set_hotspot(&eyes,true);
  eyes_set_face_angle(&eyes,rot?33:0);eyes_set_idle_rates(&eyes,0,Q16_ONE,0);
  const float bars[8]={.8,.5,.9,.6,.4,.7,.3,.6},x[3]={-.5,.6,0},y[3]={.4,.6,.3};
  eyes_set_bar_heights(&eyes,0,bars);eyes_set_bar_heights(&eyes,1,bars);
  eyes_set_spots(&eyes,2,x,y,.3); eyes_set_disco(&eyes,.13,5);eyes_set_fx(&eyes,fx,1);
  clock_t setup=0,draw=0;
  for(int i=0;i<300;i++) {
   clock_t t=clock();eyes_update(&eyes,(i+1)*16,sh);setup+=clock()-t;t=clock();
   for(int e=0;e<2;e++) for(int y=sh[e].py0;y<sh[e].py1;y+=32) {
    int rows=sh[e].py1-y<32?sh[e].py1-y:32;
    raster_band(band,sh[e].px0,y,sh[e].px1-sh[e].px0,rows,sh,2);
   } draw+=clock()-t;
  }
  printf("fx=%d angle=%d setup=%.2f raster=%.2f total=%.2f us/frame\n",fx,rot?33:0,setup*1e6/CLOCKS_PER_SEC/300,draw*1e6/CLOCKS_PER_SEC/300,(setup+draw)*1e6/CLOCKS_PER_SEC/300);
 }
 static dance_lasers_t laser;
 eyes_init(&eyes,0);eyes_set_hotspot(&eyes,true);eyes_set_idle_rates(&eyes,0,Q16_ONE,0);eyes_update(&eyes,16,sh);
 clock_t setup=0,draw=0;double pixels=0;
 for(int i=0;i<300;i++){
  audio_features_t a={.loud=.8f,.bass=.7f,.kick=.7f,.beat_count=i/12,.last_beat_ms=1000+(i/12)*408};
  clock_t t=clock();dance_lasers_update(&laser,1,&a,1000+i*34,0);setup+=clock()-t;t=clock();
  int x0=laser.damage[0]&~15,y0=laser.damage[1]&~15,x1=(laser.damage[2]+15)&~15,y1=(laser.damage[3]+15)&~15;
  if(x1>466)x1=466;if(y1>466)y1=466;
  for(int e=0;e<2;e++){
   if(sh[e].px0<x0)x0=sh[e].px0&~15;
   if(sh[e].px1>x1)x1=(sh[e].px1+15)&~15;
   if(sh[e].py0<y0)y0=sh[e].py0&~15;
   if(sh[e].py1>y1)y1=(sh[e].py1+15)&~15;
  }
  if(x1>466)x1=466;if(y1>466)y1=466;
  for(int y=y0;y<y1;y+=32)dance_lasers_paint(&laser,band,x0,y,x1-x0,y1-y<32?y1-y:32,sh);
  draw+=clock()-t;pixels+=(x1-x0)*(y1-y0);
 }
 printf("lasers upright update+damage raster=%.2f us/frame, avg %.0f pixels / %.2f ms ideal 80 MHz QSPI transfer\n",(setup+draw)*1e6/CLOCKS_PER_SEC/300,pixels/300,pixels/300*2/40000);
}
