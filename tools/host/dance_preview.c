/* Actual firmware animation/compositor, with host-only labels and disc crop.
 * From repo root: tools/host/bin/dance_preview */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "anim.h"
#include "dance_lasers.h"
#include "gfx.h"
uint32_t esp_random(void) { return 0x12345678u; }
enum { W=466, CELL=233, CW=CELL*4, CH=CELL+32, SECONDS=12 };
typedef struct { eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; dance_lasers_t lasers; } scene_t;
static scene_t scenes[4];
static uint16_t band[W*32];
static unsigned char rgb[CW*CH*3];
int main(void)
{
    FILE *video=fopen("tools/host/out/dance.rgb","wb");
    if(!video){perror("dance.rgb");return 1;}
    const char *labels[]={"SPECTRUM","MIRROR BALL","SPOTLIGHTS","LASERS"};
    for(int i=0;i<4;i++) {
        scene_t *s=&scenes[i];eyes_init(&s->eyes,0);eyes_set_hotspot(&s->eyes,true);
        anim_init(&s->sm,&s->eyes,0);anim_set(&s->sm,&s->eyes,ANIM_DANCE,0);
        s->sm.dance_visual=i+1;s->sm.dance_visual_ms=0;s->sm.dance_visual_len=100000;
    }
    uint32_t beats=0,last=0;
    for(uint32_t frame=0;frame<SECONDS*60;frame++) {
        uint32_t t=frame*1000/60;
        bool breakdown=t>=4000&&t<8000;
        if(!breakdown && t/400!=last/400){beats++;last=t-t%400;}
        float kick=breakdown?0.f:expf(-(float)(t%400)/65.f);
        audio_features_t a={.active=true,.raw_loud=300,.loud=.55f,.bass=.15f+.65f*kick,
            .kick=kick,.beat_count=beats,.last_beat_ms=last,.bpm=150,.regularity=1,.tempo_conf=breakdown?0:1};
        for(int k=0;k<16;k++) a.bands[k]=.1f+.6f*fabsf(sinf(t*.0018f+k*.5f))*(breakdown?.4f:1.f);
        for(int i=0;i<4;i++) {
            scene_t *s=&scenes[i];anim_set_audio(&s->sm,&a);anim_update(&s->sm,&s->eyes,t);
            eyes_update(&s->eyes,t,s->sh);
            dance_lasers_update(&s->lasers,s->eyes.laser_mix,s->eyes.laser_beat,t,s->eyes.face_deg);
            if(frame%2)continue; /* Animation 60 Hz; export 30 Hz. */
            for(int y=0;y<W+64;y+=32) {
                int rows=W+64-y<32?W+64-y:32;
                if(y<W) dance_lasers_paint(&s->lasers,band,0,y,W,rows,s->sh);
                else memset(band,0,sizeof band);
                const gfx_band_t gb={band,0,y,W,rows};
                gfx_ring(&gb,233,233,232,1,0,360,gfx_rgb(35,39,45));
                gfx_text(&gb,&font_spleen_16x32,(W-gfx_text_width(&font_spleen_16x32,labels[i]))/2,
                    W+3,labels[i],gfx_rgb(190,196,205),GFX_TRANSPARENT);
                const char *state=breakdown?"BREAKDOWN / COAST":t>=8000?"KICK RETURNS":"150 BPM";
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
                    unsigned char *p=&rgb[(((y+yy)/2)*CW+i*CELL+x/2)*3];
                    for(int c=0;c<3;c++)p[c]=(unsigned char)(sums[c]/4);
                }
            }
        }
        if(!(frame%2))fwrite(rgb,1,sizeof rgb,video);
    }
    int failed=ferror(video);fclose(video);return failed?1:0;
}
