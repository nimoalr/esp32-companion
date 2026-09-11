/* Exact decoded on-device effects: bonk, crack, shatter, debris, combined hits. */
#include <stdio.h>
#include <stdint.h>
#include "sfx.h"
static void u32(FILE *f,uint32_t x){for(int i=0;i<4;i++)fputc(x>>(8*i),f);}
static void u16(FILE *f,uint16_t x){fputc(x,f);fputc(x>>8,f);}
int main(void)
{
    FILE *f=fopen("docs/voice/glass_foley.wav","wb");if(!f)return 1;
    unsigned n=16000*7;fwrite("RIFF",1,4,f);u32(f,36+n*2);fwrite("WAVEfmt ",1,8,f);u32(f,16);u16(f,1);u16(f,1);
    u32(f,16000);u32(f,32000);u16(f,2);u16(f,16);fwrite("data",1,4,f);u32(f,n*2);
    sfx_t fx={0};
    for(unsigned ms=0;ms<7000;ms+=10) {
        if(ms==150||ms==4500||ms==5500)sfx_start(&fx,SFX_BONK,1);
        if(ms==1200||ms==4500)sfx_start(&fx,SFX_GLASS,.8f);
        if(ms==2200||ms==5500)sfx_start(&fx,SFX_SHATTER,.9f);
        if(ms==3700)sfx_start(&fx,SFX_TINKLE,.55f);
        int16_t pcm[160]={0};sfx_mix(&fx,pcm,160);for(int i=0;i<160;i++)u16(f,pcm[i]);
    }
    return fclose(f)!=0;
}
