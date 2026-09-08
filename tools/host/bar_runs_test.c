#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../main/raster.c"
static uint16_t fast[466],ref[466],lut[2048];
int main(void){
 uint32_t seed=17;for(int i=0;i<2048;i++)lut[i]=i;
 for(int ncase=0;ncase<15000;ncase++){
  seed=seed*1664525u+1013904223u;
  raster_shape_t s={.fx=RASTER_FX_BARS,.fx_mix=256,.lut2=lut,.bar_dim=4,.bar_lit=31,
    .cx=Q16(233)+(int)(seed&65535),.cy=Q16(233),.hw=Q16(20+(seed%140))};
  s.bar_w=s.hw/4;for(int i=0;i<8;i++){seed=seed*1664525u+1013904223u;s.bar_top[i]=(int)(seed%Q16(300))-Q16(150);}
  int x=seed%420,n=1+seed%(466-x),y=80+seed%300;
  fill_bars(fast,n,&s,x,y);bar_walk_t w={0};bar_walk_begin(&s,x,y,&w);
  for(int i=0;i<n;i++)ref[i]=lut[63+(bar_walk_level(&s,&w)<<6)];
  assert(!memcmp(fast,ref,n*2));
 }
 puts("PASS 15,000 fractional/clipped upright spectrum runs match per-pixel reference");
}
