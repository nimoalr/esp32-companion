#include "dance_lasers.h"
#include <math.h>
#include <string.h>
static uint32_t hash(uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
static void include_beams(dance_lasers_t *l)
{
    if(!l->active)return;
    for(int i=0;i<4;i++) {
        const raster_shape_t *s=&l->beam[i];if(!s->visible)continue;
        if(s->px0<l->damage[0])l->damage[0]=s->px0;
        if(s->py0<l->damage[1])l->damage[1]=s->py0;
        if(s->px1>l->damage[2])l->damage[2]=s->px1;
        if(s->py1>l->damage[3])l->damage[3]=s->py1;
    }
}
bool dance_lasers_update(dance_lasers_t *l,float mix,uint32_t beat,uint32_t now,float face)
{
    const bool on=mix>.01f;
    const uint32_t bucket=(uint32_t)((uint64_t)now*30/1000);
    if(on&&l->active&&bucket==l->bucket)return false;
    l->damage[0]=l->damage[1]=466;l->damage[2]=l->damage[3]=0;
    include_beams(l);
    if(!on){bool changed=l->active;l->active=false;return changed;}
    bool first=!l->active;l->active=true;l->bucket=bucket;
    const float fc=cosf(face*.0174532925f),fs=sinf(face*.0174532925f);
    const uint32_t pattern=hash(beat/4+37u);
    static const uint8_t rgb[4][3]={{20,180,85},{25,125,220},{145,30,210},{20,170,180}};
    for(int i=0;i<4;i++){
        const float spread=(pattern&1)?(i-1.5f)*14.f:((float)(hash(pattern+i)%110)-55.f);
        const float target=-90.f+spread;
        l->aim[i]=first?target:l->aim[i]+(target-l->aim[i])*.18f;
        const float theta=(l->aim[i]+10.f*sinf(now*.0008f+i*.35f))*.0174532925f;
        float x0=i<2?-115.f:115.f,y0=170.f,x1=222.f*cosf(theta),y1=222.f*sinf(theta);
        const float dx=x1-x0,dy=y1-y0,inv=.8f/sqrtf(dx*dx+dy*dy);
        const float nx=-dy*inv,ny=dx*inv;
        float x[4]={x0+nx,x1+nx,x1-nx,x0-nx},y[4]={y0+ny,y1+ny,y1-ny,y0-ny};
        int32_t xy[4][2];for(int k=0;k<4;k++){
            xy[k][0]=(int32_t)((233.f+x[k]*fc-y[k]*fs)*Q16_ONE);
            xy[k][1]=(int32_t)((233.f+x[k]*fs+y[k]*fc)*Q16_ONE);
        }
        const uint8_t *col=rgb[(i+(pattern>>8))%4];
        raster_build_lut(l->lut[i],(uint8_t)(col[0]*mix),(uint8_t)(col[1]*mix),(uint8_t)(col[2]*mix));
        raster_shape_t *s=&l->beam[i];memset(s,0,sizeof *s);s->lut=l->lut[i];
        raster_path_add(s,xy,4);raster_shape_finalize(s,466,466);
    }
    include_beams(l);
    return true;
}
void dance_lasers_paint(const dance_lasers_t *l,uint16_t *dst,int x,int y,int w,int rows,const raster_shape_t *eyes)
{
    if(l&&l->active){
        raster_band(dst,x,y,w,rows,l->beam,4);
        raster_shapes_over(dst,x,y,w,rows,eyes,2);
    }else raster_band(dst,x,y,w,rows,eyes,2);
}
