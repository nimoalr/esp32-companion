/* Render a lower-lid pose through the real behavior poke and eye compositor. */
#include <stdio.h>
#include <string.h>
#include "behavior.h"
#include "gfx.h"
uint32_t esp_random(void){return 17;}
static eyes_t eyes;
static raster_shape_t shapes[2];
static uint16_t fb[466*530];
static unsigned char canvas[1165*265*3];
int main(void)
{
    static const uint32_t times[]={1100,1264,1600,2288,2800};
    static const char *labels[]={"BEFORE","CLOSING","POKED","OPENING","RESTORED"};
    behavior_t b;behavior_init(&b,1000);behavior_in_t in={0};behavior_out_t out;
    eyes_init(&eyes,1000);eyes_set_hotspot(&eyes,true);eyes_set_idle_rates(&eyes,0,Q16_ONE,0);
    eye_pose_t pose=EYE_POSE_NEUTRAL;
    pose.lid_top=Q16(.15);pose.lid_bottom=Q16(.3);pose.curve=Q16(.2);
    for(int e=0;e<2;e++)eyes_set_target(&eyes,e,&pose,0,1000);
    uint32_t t=1000;
    for(int panel=0;panel<5;panel++) {
        for(;t<=times[panel];t+=4){
            if(t>=1200){in.tap_count=1;in.poke_eye=1;}
            behavior_update(&b,&in,t,&out);
            for(int e=0;e<2;e++)eyes_set_env(&eyes,e,&out.env[e]);
            eyes_update(&eyes,t,shapes);
        }
        memset(fb,0,sizeof fb);
        raster_band(fb,0,0,466,466,shapes,2);
        const gfx_band_t gb={fb,0,0,466,530};
        gfx_ring(&gb,233,233,232,1,0,360,gfx_rgb(35,39,45));
        gfx_text(&gb,&font_spleen_16x32,(466-gfx_text_width(&font_spleen_16x32,labels[panel]))/2,
            478,labels[panel],gfx_rgb(190,196,205),GFX_TRANSPARENT);
        for(int y=0;y<265;y++)for(int x=0;x<233;x++) {
            unsigned sum[3]={0};
            for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++) {
                uint16_t c=fb[(2*y+dy)*466+2*x+dx];c=(uint16_t)(c>>8|c<<8);
                sum[0]+=((c>>11)&31)*255/31;sum[1]+=((c>>5)&63)*255/63;sum[2]+=(c&31)*255/31;
            }
            unsigned char *p=&canvas[(y*1165+panel*233+x)*3];for(int k=0;k<3;k++)p[k]=sum[k]/4;
        }
    }
    FILE *f=fopen("tools/host/out/poke-preview.ppm","wb");if(!f)return 1;
    fprintf(f,"P6\n1165 265\n255\n");fwrite(canvas,1,sizeof canvas,f);return fclose(f)!=0;
}
