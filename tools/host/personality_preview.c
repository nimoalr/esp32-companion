/* Real behavior and animation playback for three scripted situations.
 * Purr input represents the mouth playing; this preview contains no audio. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "anim.h"
#include "behavior.h"
#include "gfx.h"
uint32_t esp_random(void) { return 0x12345678u; }
enum { W=466, CELL=233, CW=CELL*3, CH=CELL+32, SECONDS=12 };
typedef struct { eyes_t eyes; anim_sm_t sm; raster_shape_t sh[2]; behavior_t beh; behavior_in_t in; } scene_t;
static scene_t scenes[3];
static uint16_t band[W*32];
static unsigned char rgb[CW*CH*3];
int main(void)
{
    FILE *video=fopen("tools/host/out/personality.rgb","wb");
    if(!video){perror("personality.rgb");return 1;}
    const char *labels[]={"PETTING / PURR","KNOCK / RECOVERY","CONVERSATION"};
    for(int i=0;i<3;i++) {
        scene_t *s=&scenes[i];eyes_init(&s->eyes,1000);eyes_set_hotspot(&s->eyes,true);
        anim_init(&s->sm,&s->eyes,1000);behavior_init(&s->beh,1000);s->in.idle_allowed=true;
    }
    for(uint32_t frame=0;frame<SECONDS*60;frame++) {
        uint32_t elapsed=frame*1000/60, t=1000+elapsed;
        for(int i=0;i<3;i++) {
            scene_t *s=&scenes[i];
            if(i==0) {
                s->in.stroke_forehead=true;s->in.stroke_count=elapsed<600?0:elapsed<1200?1:2;
                s->in.user_interacting=elapsed>=600&&elapsed<5200;
                s->in.purring=elapsed>=1600&&elapsed<6600;
            } else if(i==1) {
                if(frame==60)s->beh.pending=BEH_EV_BODY_TAP;
            } else {
                s->in.audio.active=true;s->in.audio.speech=elapsed>=1000&&elapsed<5000;
                /* A spoken acknowledgement accepted by the mouth at this instant. */
                if(frame==180)behavior_cue(&s->beh,ANIM_NOD,t);
            }
            s->in.shown_anim=s->sm.id;s->in.shown_anim_done=t-s->sm.t_change_ms>=anim_action_ms(s->sm.id);
            behavior_out_t out;behavior_update(&s->beh,&s->in,t,&out);
            anim_id_t want=out.override_anim<0?ANIM_NEUTRAL:(anim_id_t)out.override_anim;
            if(want!=s->sm.id)anim_set(&s->sm,&s->eyes,want,t);
            for(int e=0;e<2;e++)eyes_set_env(&s->eyes,e,&out.env[e]);
            anim_update(&s->sm,&s->eyes,t);eyes_update(&s->eyes,t,s->sh);
            if(frame%2)continue; /* Animation 60 Hz; export 30 Hz. */
            for(int y=0;y<W+64;y+=32) {
                int rows=W+64-y<32?W+64-y:32;
                if(y<W) raster_band(band,0,y,W,rows,s->sh,2);
                else memset(band,0,sizeof band);
                const gfx_band_t gb={band,0,y,W,rows};
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
                    unsigned char *p=&rgb[(((y+yy)/2)*CW+i*CELL+x/2)*3];
                    for(int c=0;c<3;c++)p[c]=(unsigned char)(sums[c]/4);
                }
            }
        }
        if(!(frame%2))fwrite(rgb,1,sizeof rgb,video);
    }
    int failed=ferror(video);fclose(video);return failed?1:0;
}
