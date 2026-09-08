#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "glass.h"
#include "play.h"
#include "accessories.h"
uint32_t esp_random(void){return 17;}
static uint16_t old[466*466],full[466*466],tile[466*466];
static unsigned char dirty[466*466];
int main(void)
{
    glass_t g={0};glass_rect_t r[GLASS_MAX_DIRTY];unsigned released=0,updates=0,peak=0;unsigned long area=0;
    for(int i=0;i<466*466;i++)old[i]=0x4110;
    clock_t begin=clock();
    for(unsigned t=0;t<7100;t+=16) {
        int n=glass_update(&g,t<6500?4:0,t,1000+t,r);assert(n<=GLASS_MAX_DIRTY);
        memset(dirty,0,sizeof dirty);unsigned pixels=0;
        for(int i=0;i<n;i++){pixels+=(r[i].x1-r[i].x0)*(r[i].y1-r[i].y0);for(int y=r[i].y0;y<r[i].y1;y++)memset(dirty+y*466+r[i].x0,1,r[i].x1-r[i].x0);}
        if(n){updates++;area+=pixels;if(pixels>peak)peak=pixels;}
        if(g.stage==4){assert(g.released>=released);released=g.released;}
        for(int i=0;i<466*466;i++)full[i]=tile[i]=0x4110;
        gfx_band_t b={full,0,0,466,466};glass_paint(&g,&b);
        for(int y=0;y<466;y+=7){gfx_band_t band={tile+y*466,0,y,466,466-y<7?466-y:7};glass_paint(&g,&band);}
        assert(!memcmp(full,tile,sizeof full));
        if(t<900) {
            for(int i=0;i<466*466;i++)tile[i]=0x4110;
            gfx_band_t original={tile,0,0,466,466};cracks_paint(&original,4);
            assert(!memcmp(full,tile,sizeof full)); /* no new tessellation at the final hit */
        }
        for(int i=0;i<466*466;i++)if(full[i]!=old[i])assert(dirty[i]);
        memcpy(old,full,sizeof old);
    }
    assert(released==9&&!g.hole&&g.stage==0);
    for(int i=0;i<466*466;i++)assert(old[i]==0x4110);
    printf("PASS all nine shards release separately, full/tiled equality, exact old/new damage, hole removal; %u updates, avg %lu / peak %u pixels, %.1f host ms including validation\n",updates,area/updates,peak,(clock()-begin)*1000./CLOCKS_PER_SEC);
    accessories_t a;acc_init(&a,138,328,233);acc_rect_t ar[ACC_MAX_DIRTY];memset(old,0,sizeof old);
    for(unsigned t=0;t<2400;t+=16) {
        acc_set_anger(&a,t<1000);acc_set_angle(&a,t<1200?t*.05f:60);int n=acc_update(&a,1000+t,ar);
        memset(dirty,0,sizeof dirty);memset(full,0,sizeof full);
        for(int i=0;i<n;i++)for(int y=ar[i].y0;y<ar[i].y1;y++)memset(dirty+y*466+ar[i].x0,1,ar[i].x1-ar[i].x0);
        gfx_band_t b={full,0,0,466,466};acc_paint(&a,&b,1000+t);
        for(int i=0;i<466*466;i++)if(old[i]!=full[i])assert(dirty[i]);memcpy(old,full,sizeof old);
    }
    assert(!a.anger_mix);puts("PASS anger mark pulse, rotation, fade and damage cleanup");
}
