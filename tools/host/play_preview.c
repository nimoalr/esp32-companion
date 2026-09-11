/* Actual firmware animation/compositor, with host-only labels and disc crop.
 * From repo root: tools/host/bin/dance_preview */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "anim.h"
#include "play.h"
#include "gfx.h"
uint32_t esp_random(void) { return 0x12345678u; }
enum { W=466, CELL=233, CW=CELL*3, CH=(CELL+32)*2, SECONDS=12 };
typedef struct { eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; unsigned serial; } scene_t;
static scene_t scenes[6];
static uint16_t band[W*32];
static unsigned char rgb[CW*CH*3];
int main(void)
{
    FILE *video=fopen("tools/host/out/play.rgb","wb");
    if(!video){perror("play.rgb");return 1;}
    const char *labels[]={"PUCKS","SLOT MACHINE","SEASICK","CROSS EYED","JELLY","HEADBUTT"};
    const anim_id_t ids[]={ANIM_PUCKS,ANIM_HIGH_ROLLER,ANIM_SEASICK,ANIM_CROSS_EYED,ANIM_JELLY,ANIM_HEADBUTT};
    for(int i=0;i<6;i++) {
        scene_t *s=&scenes[i];eyes_init(&s->eyes,0);eyes_set_hotspot(&s->eyes,true);
        anim_init(&s->sm,&s->eyes,0);anim_set(&s->sm,&s->eyes,ids[i],0);
    }
    for(uint32_t frame=0;frame<SECONDS*60;frame++) {
        uint32_t t=frame*1000/60;
        for(int i=0;i<6;i++) {
            scene_t *s=&scenes[i];
            if(i==5&&frame&&frame%180==0)anim_set(&s->sm,&s->eyes,ANIM_HEADBUTT,t);
            if((i==0&&frame==480)||(i==1&&frame==372)||(i==4&&frame==420))anim_set(&s->sm,&s->eyes,ANIM_DIZZY,t);
            s->sm.motion_x=.5f*sinf(t*.001f);s->sm.motion_y=.5f*cosf(t*.0013f);
            anim_update(&s->sm,&s->eyes,t);eyes_update(&s->eyes,t,s->sh);
            if(frame%2)continue; /* Animation 60 Hz; export 30 Hz. */
            for(int y=0;y<W+64;y+=32) {
                int rows=W+64-y<32?W+64-y:32;
                if(y<W) raster_band(band,0,y,W,rows,s->sh,2);
                else memset(band,0,sizeof band);
                const gfx_band_t gb={band,0,y,W,rows};
                if(i==5)cracks_paint(&gb,(int)(t/3000)+(t%3000>=500?1:0));
                gfx_ring(&gb,233,233,232,1,0,360,gfx_rgb(35,39,45));
                gfx_text(&gb,&font_spleen_16x32,(W-gfx_text_width(&font_spleen_16x32,labels[i]))/2,
                    W+3,labels[i],gfx_rgb(190,196,205),GFX_TRANSPARENT);
                const char *state=anim_name(s->sm.id);
                gfx_text(&gb,&font_spleen_8x16,(W-gfx_text_width(&font_spleen_8x16,state))/2,
                    W+42,state,gfx_rgb(140,155,170),GFX_TRANSPARENT);
                for(int yy=0;yy<rows;yy+=2) for(int x=0;x<W;x+=2) {
                    int sums[3]={0};
                    for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++) {
                        int py=y+yy+dy,px=x+dx;
                        uint16_t c=band[(yy+dy)*W+px];c=(uint16_t)(c>>8|c<<8);
                        if(py<W && (2*px+1-W)*(2*px+1-W)+(2*py+1-W)*(2*py+1-W)>W*W)c=0;
                        if(py>=W && py<W+2)c=0;
                        sums[0]+=((c>>11)&31)*255/31;sums[1]+=((c>>5)&63)*255/63;sums[2]+=(c&31)*255/31;
                    }
                    unsigned char *p=&rgb[(((y+yy)/2+(i/3)*(CELL+32))*CW+(i%3)*CELL+x/2)*3];
                    for(int c=0;c<3;c++)p[c]=(unsigned char)(sums[c]/4);
                }
            }
        }
        if(!(frame%2))fwrite(rgb,1,sizeof rgb,video);
    }
    int failed=ferror(video);fclose(video);return failed?1:0;
}
