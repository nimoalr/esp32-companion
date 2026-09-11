/* PCM examples of the exact on-board procedural SFX. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "anim.h"
uint32_t esp_random(void){return 17;}
static eyes_t eyes;static anim_sm_t sm;static sfx_t fx;
static void u32(FILE *f,uint32_t x){for(int i=0;i<4;i++)fputc(x>>(8*i),f);}
static void u16(FILE *f,uint16_t x){fputc(x,f);fputc(x>>8,f);}
int main(void)
{
    const unsigned samples=16000*16;
    FILE *f=fopen("docs/expressions/play-sounds.wav","wb");if(!f)return 1;
    fwrite("RIFF",1,4,f);u32(f,36+samples*2);fwrite("WAVEfmt ",1,8,f);u32(f,16);u16(f,1);u16(f,1);
    u32(f,16000);u32(f,32000);u16(f,2);u16(f,16);fwrite("data",1,4,f);u32(f,samples*2);
    eyes_init(&eyes,0);anim_init(&sm,&eyes,0);anim_set(&sm,&eyes,ANIM_PUCKS,0);unsigned serial=0;
    for(unsigned ms=0;ms<16000;ms+=10){
        if(ms==5000)anim_set(&sm,&eyes,ANIM_HIGH_ROLLER,ms);
        if(ms>=12000&&(ms-12000)%1000==0){sfx_start(&fx,SFX_BONK,.8f);if(ms>12000)sfx_start(&fx,ms==15000?SFX_SHATTER:SFX_GLASS,.8f);}
        sm.motion_x=1.2f*sinf(ms*.014f);sm.motion_y=.8f*cosf(ms*.011f);
        if(ms<12000){anim_update(&sm,&eyes,ms);if(sm.effect_serial!=serial){serial=sm.effect_serial;sfx_start(&fx,sm.effect,sm.effect_level);}}
        int16_t pcm[160]={0};sfx_mix(&fx,pcm,160);for(int i=0;i<160;i++)u16(f,(uint16_t)pcm[i]);
    }
    return fclose(f)!=0;
}
