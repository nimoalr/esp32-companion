/* Firmware-rendered review: loose eyes, escalating glass, fades and the dim phase. */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "anim.h"
#include "behavior.h"
#include "glass.h"
#include "accessories.h"
#include "dance_lasers.h"
uint32_t esp_random(void){return 17;}
typedef struct {eyes_t eyes;anim_sm_t sm;raster_shape_t shape[2];accessories_t acc;glass_t glass;dance_lasers_t lights;} scene_t;
static scene_t scenes[4];static uint16_t fb[466*466];static uint8_t rgb[466*530*3];
static void u32(FILE *f,uint32_t x){for(int i=0;i<4;i++)fputc(x>>(8*i),f);}
static void u16(FILE *f,uint16_t x){fputc(x,f);fputc(x>>8,f);}
int main(void)
{
    const char *labels[]={"LOOSE EYES","GLASS / ATTITUDE","DANCE FADES","DIM PHASE (40x)"};
    FILE *f=fopen("tools/host/out/refinements.rgb","wb");if(!f)return 1;
    FILE *sound=fopen("tools/host/out/refinements_audio.wav","wb");if(!sound)return 1;
    unsigned samples=18*16000;fwrite("RIFF",1,4,sound);u32(sound,36+samples*2);fwrite("WAVEfmt ",1,8,sound);u32(sound,16);u16(sound,1);u16(sound,1);
    u32(sound,16000);u32(sound,32000);u16(sound,2);u16(sound,16);fwrite("data",1,4,sound);u32(sound,samples*2);
    sfx_t fx={0};unsigned effect_seen=0;int stage_seen=0;
    for(int i=0;i<4;i++){eyes_init(&scenes[i].eyes,1000);eyes_set_hotspot(&scenes[i].eyes,true);anim_init(&scenes[i].sm,&scenes[i].eyes,1000);acc_init(&scenes[i].acc,138,328,233);}
    for(unsigned frame=0;frame<18*60;frame++) {
        unsigned age=frame*1000/60,now=1000+age;
        for(int i=0;i<4;i++) {
            scene_t *s=&scenes[i];anim_id_t id=ANIM_NEUTRAL;
            if(i==0){id=age<8500?ANIM_PUCKS:ANIM_DIZZY;s->sm.motion_x=age%3500<2300?1.3f*sinf(age*.017f):0;s->sm.motion_y=age%3500<2300?.9f*cosf(age*.013f):0;}
            if(i==1){id=age<12000?ANIM_HEADBUTT:ANIM_ANGRY;if(age<12000 && frame%180==0)anim_set(&s->sm,&s->eyes,id,now);}
            if(i==2)id=age<7000?ANIM_DANCE:age<10000?ANIM_NEUTRAL:age<14000?ANIM_DANCE:ANIM_HEARTS;
            if(i==3)id=behavior_doze_face(30000+age*40);
            if(s->sm.id!=id)anim_set(&s->sm,&s->eyes,id,now);
            audio_features_t a={.active=true,.raw_loud=300,.loud=.7f,.bass=.5f,.kick=.8f,.bpm=150,.beat_count=1+age/400,.last_beat_ms=1000+age/400*400};
            anim_set_audio(&s->sm,&a);
            if(i==2 && id==ANIM_DANCE){s->sm.dance_visual=2;s->sm.dance_visual_len=100000;s->sm.dance_lasers_on=s->sm.dance_spots_on=true;s->sm.dance_laser_len=s->sm.dance_spot_len=100000;}
            anim_update(&s->sm,&s->eyes,now);eyes_update(&s->eyes,now,s->shape);
            dance_background_update(&s->lights,s->eyes.laser_mix,s->eyes.spot_mix,&a,now,0);
            int stage=i==1?(age<12000?(int)(age/3000)+(age%3000>=500):4):0;
            if(age>=16000)stage=0;
            unsigned released=s->glass.released;glass_rect_t gr[GLASS_MAX_DIRTY];glass_update(&s->glass,stage,age>=9500?age-9500:0,now,gr);
            if(i==1) {
                if(stage==4 && s->glass.released>released)sfx_start(&fx,SFX_TINKLE,.55f);
                if(s->sm.effect_serial!=effect_seen){effect_seen=s->sm.effect_serial;sfx_start(&fx,s->sm.effect,s->sm.effect_level);}
                if(stage>stage_seen&&stage>=2)sfx_start(&fx,stage==4?SFX_SHATTER:SFX_GLASS,stage==4?.9f:.8f);
                stage_seen=stage;
            }
            acc_set_anger(&s->acc,i==1&&age>=3500);acc_rect_t ar[ACC_MAX_DIRTY];acc_update(&s->acc,now,ar);
            if(frame%2)continue;
            for(int y=0;y<466;y+=16){int rows=466-y<16?466-y:16;dance_lasers_paint(&s->lights,fb+y*466,0,y,466,rows,s->shape);gfx_band_t b={fb+y*466,0,y,466,rows};glass_paint(&s->glass,&b);acc_paint(&s->acc,&b,now);}
            for(int y=0;y<233;y++)for(int x=0;x<233;x++) {
                unsigned sum[3]={0};for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++){
                    int px=x*2+xx,py=y*2+yy;uint16_t c=fb[py*466+px];c=c>>8|c<<8;
                    if((px-233)*(px-233)+(py-233)*(py-233)>232*232)c=0;
                    sum[0]+=((c>>11)&31)*255/31;sum[1]+=((c>>5)&63)*255/63;sum[2]+=(c&31)*255/31;
                }
                unsigned offset=((y+(i/2)*265)*466+x+(i%2)*233)*3;for(int c=0;c<3;c++)rgb[offset+c]=sum[c]/4;
            }
            memset(fb,0,466*64*2);gfx_band_t label={fb,0,0,466,64};gfx_text(&label,&font_spleen_16x32,(466-gfx_text_width(&font_spleen_16x32,labels[i]))/2,0,labels[i],gfx_rgb(200,205,210),GFX_TRANSPARENT);
            for(int y=0;y<32;y++)for(int x=0;x<233;x++){uint16_t c=fb[y*2*466+x*2];c=c>>8|c<<8;unsigned o=((233+y+(i/2)*265)*466+x+(i%2)*233)*3;rgb[o]=((c>>11)&31)*255/31;rgb[o+1]=((c>>5)&63)*255/63;rgb[o+2]=(c&31)*255/31;}
        }
        int n=(frame+1)*16000/60-frame*16000/60;int16_t pcm[267]={0};sfx_mix(&fx,pcm,n);for(int i=0;i<n;i++)u16(sound,pcm[i]);
        if(!(frame%2))fwrite(rgb,1,sizeof rgb,f);
    }
    int err=ferror(f)||ferror(sound);fclose(f);fclose(sound);return err?1:0;
}
