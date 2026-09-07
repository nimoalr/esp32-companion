#include "dance_lasers.h"
#include "esp_attr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu;
    return v ^ (v >> 16);
}
static float unit(float v) { return fminf(1.f, fmaxf(0.f, v)); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void include_lights(dance_lasers_t *l)
{
    if (!l->active) return;
    for(int i=0;i<6;i++)if(l->spots_on && l->spot[i].visible) {
        const raster_shape_t *s=&l->spot[i];
        l->damage[0]=imin(l->damage[0],s->px0);l->damage[1]=imin(l->damage[1],s->py0);
        l->damage[2]=imax(l->damage[2],s->px1);l->damage[3]=imax(l->damage[3],s->py1);
    }
    for (int i = 0; i < DANCE_LASER_MAX; i++) {
        const dance_ray_t *b = &l->beam[i];
        if (!b->light) continue;
        for (int k = 0; k < 2; k++) {
            l->damage[k] = imin(l->damage[k], b->bounds[k]);
            l->damage[k+2] = imax(l->damage[k+2], b->bounds[k+2]);
        }
    }
}

/* Each ray starts at its row/rim fixture and ends at the circular display
 * boundary, including beams which cross through the centre.
 * Sort along its major axis once; the band painter only increments integers. */
static void ray_geometry(dance_ray_t *b, int x0,int y0, float radians)
{
    b->origin_x = x0;b->origin_y=y0;
    const float cs = cosf(radians), sn = sinf(radians), ox=x0-233.f,oy=y0-233.f,dot=ox*cs+oy*sn;
    const float length = -dot + sqrtf(fmaxf(0,230.f*230.f-ox*ox-oy*oy+dot*dot));
    const int x1 = (int)lroundf(x0 + length*cs);
    const int y1 = (int)lroundf(y0 + length*sn);
    b->steep = abs(y1-y0) > abs(x1-x0);
    int a0 = b->steep ? y0 : x0, a1 = b->steep ? y1 : x1;
    int c0 = b->steep ? x0 : y0, c1 = b->steep ? x1 : y1;
    if (a0 > a1) { int t = a0; a0 = a1; a1 = t; t = c0; c0 = c1; c1 = t; }
    b->major0 = a0; b->major1 = a1;
    b->minor0 = c0 * Q16_ONE;
    b->step = (int32_t)((int64_t)(c1-c0) * Q16_ONE / (a1==a0?1:a1-a0));
    b->bounds[0] = imax(0, imin(x0,x1)-1);
    b->bounds[1] = imax(0, imin(y0,y1)-1);
    b->bounds[2] = imin(466, imax(x0,x1)+2);
    b->bounds[3] = imin(466, imax(y0,y1)+2);
}

static void fixture(int layout,int i,int n,uint32_t seed,int *x,int *y)
{
    if(layout==0){*x=35+i*396/(n-1);*y=233;}
    else {float a=i*6.2831853f/n+(seed%628)*.01f;*x=233+(int)(218*cosf(a));*y=233+(int)(218*sinf(a));}
}
static void spot_geometry(dance_lasers_t *l,float mix,const audio_features_t *a,uint32_t now)
{
    static const uint8_t color[4][3]={{255,110,35},{40,130,255},{200,50,255},{55,255,175}};
    float t=(now%3600000)*.001f;
    for(int i=0;i<3;i++) {
        int x,y;fixture(l->spot_arrangement,i,3,l->spot_seed,&x,&y);
        float phase=t*(.35f+(l->spot_seed%7)*.025f)+i*2.094f;
        float tx=233+125*sinf(phase),ty=233+145*cosf(phase*.83f+i*.35f);
        float dx=tx-x,dy=ty-y,d=hypotf(dx,dy);if(d<1)d=1;
        float r=14+10*unit(a->bass),nx=-dy/d,ny=dx/d;
        const uint8_t *rgb=color[(i+l->spot_seed/17)%4];
        float level=unit(mix)*(.55f+.45f*unit(a->loud));
        for(int k=0;k<2;k++) {
            raster_shape_t *s=&l->spot[2*i+k];memset(s,0,sizeof *s);
            float brightness=level*(k?.70f:.22f);
            raster_build_lut(l->spot_lut[2*i+k],rgb[0]*brightness,rgb[1]*brightness,rgb[2]*brightness);
            s->lut=l->spot_lut[2*i+k];
            if(!k){int32_t xy[4][2]={{(int32_t)((x+nx*2)*Q16_ONE),(int32_t)((y+ny*2)*Q16_ONE)},
                {(int32_t)((tx+nx*r)*Q16_ONE),(int32_t)((ty+ny*r)*Q16_ONE)},
                {(int32_t)((tx-nx*r)*Q16_ONE),(int32_t)((ty-ny*r)*Q16_ONE)},
                {(int32_t)((x-nx*2)*Q16_ONE),(int32_t)((y-ny*2)*Q16_ONE)}};raster_path_add(s,xy,4);
            } else {s->cx=(int32_t)(tx*Q16_ONE);s->cy=(int32_t)(ty*Q16_ONE);s->hw=(int32_t)(r*Q16_ONE);s->hh=(int32_t)(r*.6f*Q16_ONE);
                s->top_base=s->cy-s->hh;s->bot_base=s->cy+s->hh;
                for(int j=0;j<4;j++){s->rad[j]=s->hw;s->rady[j]=s->hh;}}
            raster_shape_finalize(s,466,466);
        }
    }
}
bool dance_lasers_update(dance_lasers_t *l,float mix,const audio_features_t *a,uint32_t now,float face)
{return dance_background_update(l,mix,0,a,now,face);}

bool dance_background_update(dance_lasers_t *l, float mix,float spots, const audio_features_t *a,
                         uint32_t now, float face)
{
    (void)face; /* Fixture rigs stay in display coordinates, independent of face orientation. */
    const bool laser_on=mix>.01f,spot_on=spots>.01f,on=laser_on||spot_on;
    const uint32_t bucket = (uint32_t)((uint64_t)now * 30 / 1000);
    if (on && l->active && bucket == l->bucket) return false;
    l->damage[0] = l->damage[1] = 466; l->damage[2] = l->damage[3] = 0;
    include_lights(l);
    if (!on) { bool changed = l->active; l->active = l->laser_on=l->spots_on=false; return changed; }

    const bool first = !l->laser_on;
    if(laser_on&&!l->laser_on){l->laser_seed=hash(now+17);l->arrangement=l->laser_seed%3==0;}
    if(spot_on&&!l->spots_on){l->spot_seed=hash(now+713);l->spot_arrangement=l->spot_seed%2;}
    l->laser_on=laser_on;l->spots_on=spot_on;
    if(spot_on)spot_geometry(l,spots,a,now);
    const float dt = first ? 34.f : fminf(100.f, (float)(now-l->updated_ms));
    l->active = true; l->bucket = bucket; l->updated_ms = now;
    if (!l->palette_ready) {
        static const uint8_t rgb[4][3] = {{30,215,90},{30,165,245},{195,40,235},{25,205,200}};
        for (int i = 0; i < 4; i++) raster_build_lut(l->lut[i], rgb[i][0], rgb[i][1], rgb[i][2]);
        l->palette_ready = true;
    }
    const float loud = unit(a->loud), bass = unit(a->bass);
    const uint32_t age = now - a->last_beat_ms;
    const float hit = a->last_beat_ms && age < 300 ? unit(a->kick) * (1.f-age/300.f) : 0.f;
    const uint32_t pattern = hash(a->beat_count/16 + 37u + l->laser_seed);
    const int style = l->style=(int)(pattern % 7);
    /* Continuous sweep during breakdowns; measured music changes its opening,
     * density and brightness. Gates change only on real beats, then fade. */
    const float sweep = (now%3600000)*.00065f;
    const float spread = .40f + .35f*bass + .30f*hit;
    const float density = .46f + .30f*loud + .18f*bass;
    for (int i = 0; i < DANCE_LASER_MAX; i++) {
        float target;
        const int emitter = i/3, ray = i%3;
        int x,y;fixture(l->arrangement,emitter,DANCE_LASER_EMITTERS,l->laser_seed,&x,&y);
        float phase=sweep+(style==2?emitter*.38f:style==3?(emitter%2)*3.1415927f:0);
        /* Five of seven looks are vertical tilt sweeps; coherent fixture phase,
         * no accumulating rotation. Other looks converge or open a ring fan. */
        float vertical=(l->arrangement?y<233:style==1&&(emitter&1))?1.5707963f:-1.5707963f;
        if(style<5)target=vertical+.48f*sinf(phase)+(ray-1)*spread*(style==4?.6f:.25f);
        else {target=atan2f(233.f-y,233.f-x)+(ray-1)*spread*(style==5?.1f:.65f)+.24f*sinf(phase);}
        float delta = remainderf(target-l->aim[i], 6.2831853f);
        l->aim[i] = first ? target : l->aim[i] + delta * dt/(320.f+dt);
        const uint32_t gate = hash(a->beat_count*131u + i*977u);
        const float want = (gate%1000) < density*1000.f ? 1.f : 0.f;
        if (first) l->fade[i] = want;
        else l->fade[i] += (want-l->fade[i]) * dt/((want>l->fade[i]?45.f:160.f)+dt);
        dance_ray_t *b = &l->beam[i];
        const float light = (laser_on?mix:0) * l->fade[i] * (.32f+.35f*loud+.33f*hit);
        b->light = light > .025f ? (uint8_t)(255.f*unit(light)) : 0;
        b->palette = (uint8_t)((i/3 + (pattern>>8)) % 4);
        if (b->light) ray_geometry(b,x,y,l->aim[i]);
    }
    include_lights(l);
    return true;
}

/* Add two light contributions in panel byte order. Only beam pixels visit this
 * path, never the whole background. Crossing rays can brighten. */
static inline uint16_t IRAM_ATTR add_light(uint16_t dst, uint16_t col)
{
    if (!dst) return col;
    const unsigned d = (uint16_t)(dst>>8 | dst<<8), c = (uint16_t)(col>>8 | col<<8);
    unsigned r = (d>>11)+(c>>11), g = ((d>>5)&63)+((c>>5)&63), b = (d&31)+(c&31);
    if (r>31) r=31;
    if (g>63) g=63;
    if (b>31) b=31;
    unsigned sum = r<<11 | g<<5 | b;
    return (uint16_t)(sum>>8 | sum<<8);
}

void IRAM_ATTR dance_lasers_paint(const dance_lasers_t *l, uint16_t *dst, int x, int y,
                                 int w, int rows, const raster_shape_t *eyes)
{
    if (!l || !l->active) { raster_band(dst,x,y,w,rows,eyes,2); return; }
    memset(dst,0,(size_t)w*rows*sizeof *dst);
    if(l->spots_on)raster_shapes_over(dst,x,y,w,rows,l->spot,6);
    for (int i = 0; i < DANCE_LASER_MAX; i++) {
        const dance_ray_t *b = &l->beam[i];
        if (!b->light || b->bounds[0]>=x+w || b->bounds[2]<=x || b->bounds[1]>=y+rows || b->bounds[3]<=y) continue;
        const int lo = imax(b->major0, b->steep ? y : x);
        const int hi = imin(b->major1, (b->steep ? y+rows : x+w)-1);
        int32_t minor = b->minor0 + (lo-b->major0)*b->step;
        const uint16_t *lut = l->lut[b->palette];
        for (int major = lo; major <= hi; major++, minor += b->step) {
            const int m = minor>>16, frac = (minor&65535)>>8;
            /* 1.6 px beam: a bright core and two antialiased fringes. */
            for (int edge = -1; edge <= 1; edge++) {
                const int px = b->steep ? m+edge : major, py = b->steep ? major : m+edge;
                if (px<x || px>=x+w || py<y || py>=y+rows) continue;
                const unsigned coverage = edge < 0 ? (unsigned)imax(0,77-frac)
                                          : edge == 0 ? (unsigned)imin(256,333-frac)
                                                      : (unsigned)imin(256,77+frac);
                uint16_t *p = dst + (py-y)*w + px-x;
                *p = add_light(*p,lut[(coverage*b->light)>>8]);
            }
        }
    }
    raster_shapes_over(dst,x,y,w,rows,eyes,2);
}
