#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
uint32_t esp_random(void){return 17;}
static uint16_t cached[466*466],ref[466*466],tile[466*466];
int main(void){
 for(int angle=-180;angle<181;angle+=9){
  memset(cached,0,sizeof cached);memset(ref,0,sizeof ref);memset(tile,0,sizeof tile);
  uint16_t color=gfx_rgb(80+(angle+180)%170,180,120);
  gfx_band_t a={cached,0,0,466,466},b={ref,1,0,466,466};
  gfx_ring(&a,233,233,232,4,-150,angle,color);
  gfx_ring(&b,234,233,232,4,-150,angle,color); /* translated geometry bypasses cache */
  assert(!memcmp(cached,ref,sizeof ref));
  for(int y=0;y<466;y+=7){gfx_band_t t={tile+y*466,0,y,466,466-y<7?466-y:7};gfx_ring(&t,233,233,232,4,-150,angle,color);}
  assert(!memcmp(cached,tile,sizeof tile));
 }
 puts("PASS cached charge rim equals generic geometry and tiled rendering for all sweep angles");
}
