#include "dance_fill.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>
/* Projection is static: no roots, trig or division in the display pixel loop.
 * Longitude is Q8 facet columns; latitude is eight rows over the sphere. */
static int16_t longitude[64*64];
static uint8_t latitude[64*64], edge[64*64];
static float row_sin[8], row_cos[8];
static bool ready;
void dance_fill_init(void)
{
    if (ready) return;
    for (int r=0;r<8;r++) {
        float a=((r+.5f)/8.f-.5f)*3.14159265f;
        row_sin[r]=sinf(a);row_cos[r]=cosf(a);
    }
    for (int y=0;y<64;y++) for (int x=0;x<64;x++) {
        const int i=y*64+x;
        float nx=(x-31.5f)/26.f, ny=(y-34.f)/26.f;
        float rr=nx*nx+ny*ny;
        if (rr>=1.f) { latitude[i]=255; continue; }
        float lon=atan2f(nx,sqrtf(1.f-rr));
        float lat=asinf(ny)/3.14159265f*8.f+4.f;
        longitude[i]=(int16_t)(lon*(16.f*256.f/6.2831853f));
        latitude[i]=(uint8_t)lat;
        float fract=lat-floorf(lat);
        edge[i]=fract<.09f ? 1 : 0;
    }
    ready=true;
}
void dance_fill_disco(dance_fill_t *f, float turns)
{
    dance_fill_init();
    uint8_t shade[8][16];
    float phase=turns-floorf(turns);
    const int offset=(int)(phase*4096.f);
    for (int c=0;c<16;c++) {
        float a=((c+.5f)/16.f-phase)*6.2831853f;
        float sn=sinf(a),cs=cosf(a);
        for(int r=0;r<8;r++) {
            float nx=sn*row_cos[r], ny=row_sin[r], nz=cs*row_cos[r];
            float diffuse=fmaxf(0.f,-.4f*nx-.5f*ny+.768f*nz);
            float spec=fmaxf(0.f,-.22f*nx-.27f*ny+.937f*nz);
            float s2=spec*spec,s4=s2*s2,s8=s4*s4;
            int level=(int)(DANCE_FILL_DIM+12.f*diffuse+22.f*s8*s8*s8);
            shade[r][c]=(uint8_t)(level>31?31:level);
        }
    }
    for (int i=0;i<4096;i++) {
        if(latitude[i]==255) { f->tex[i]=DANCE_FILL_DIM; continue; }
        const unsigned u=(unsigned)(longitude[i]+offset+8192);
        f->tex[i]=edge[i] || (u&255)<24 ? DANCE_FILL_DIM : shade[latitude[i]][(u>>8)&15];
    }
    /* A suspension thread above the sphere, not a floating checkerboard. */
    for(int y=1;y<8;y++) { f->tex[y*64+31]=10; f->tex[y*64+32]=10; }
}
void dance_fill_spots(dance_fill_t *f,int n,const float *x,const float *y,float width)
{
    memset(f->tex,DANCE_FILL_DIM,sizeof f->tex);
    if(n>3)n=3;
    for(int k=0;k<n;k++) {
        const int source_x=k&1 ? 55 : 8, source_y=3;
        const int target_x=(int)(31.5f+24.f*x[k]);
        const int target_y=(int)(54.f+3.f*y[k]);
        const int half=(int)(7.f+width*12.f);
        for(int py=source_y;py<64;py++) {
            int along=(py-source_y)*256/(target_y-source_y);
            if(along>300)continue;
            int cx=source_x*256+(target_x-source_x)*along;
            int hw=256+half*along;
            int inv=65536/hw;
            for(int px=0;px<64;px++) {
                int d=px*256-cx;if(d<0)d=-d;
                int cross=d*inv>>8;
                if(cross>=256)continue;
                int level=4+((256-cross)*14>>8);
                int dy=py-target_y,dx=px-target_x;
                /* The beam lands in a flattened bright pool on the floor. */
                if(dx*dx*25+dy*dy*half*half < half*half*25)level+=11;
                int i=py*64+px, sum=f->tex[i]+level;
                f->tex[i]=(uint8_t)(sum>31?31:sum);
            }
        }
        for(int py=1;py<5;py++)for(int px=source_x-2;px<=source_x+2;px++)f->tex[py*64+px]=31;
    }
}
