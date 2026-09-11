#include "play.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
bool petting_update(petting_t *p, bool down, float x, float y, uint32_t now)
{
    if (!down || x<20 || x>446 || y<0 || y>185) {p->down=false;p->travel=0;p->latched=false;return false;}
    if (!p->down) {
        p->down=true;p->latched=false;p->x=p->span_x=x;p->y=p->span_y=y;p->travel=0;p->since=now;return false;
    }
    float distance=fabsf(x-p->x)+fabsf(y-p->y);p->x=x;p->y=y;
    if(distance>55){p->down=false;return false;} /* discontinuous controller sample */
    if(p->latched) {
        float progress=(x-p->peak_x)*p->dir_x+(y-p->peak_y)*p->dir_y;
        if(progress>0){p->peak_x=x;p->peak_y=y;}
        if(progress> -14)return false;
        p->span_x=p->peak_x;p->span_y=p->peak_y;p->travel=14;p->since=now;p->latched=false;
    }
    if(distance>1.5f)p->travel+=distance;
    if(p->travel>=60 && fabsf(x-p->span_x)+fabsf(y-p->span_y)>=45 && now-p->last>=350) {
        float dx=x-p->span_x,dy=y-p->span_y,n=hypotf(dx,dy);
        p->dir_x=dx/n;p->dir_y=dy/n;p->peak_x=x;p->peak_y=y;p->latched=true;
        p->count++;p->last=now;p->travel=0;return true;
    }
    return false;
}
static uint32_t random32(uint32_t *r){*r=*r*1664525u+1013904223u;return *r;}
void pucks_init(pucks_t *p,uint32_t now,uint32_t seed)
{
    (void)seed;
    memset(p,0,sizeof *p);p->ms=now;
    for(int e=0;e<2;e++) {
        p->x[e]=138+190*e;p->y[e]=233;
        p->hw[e]=41;p->hh[e]=52;p->radius[e]=32;
    }
}
static float wall(pucks_t *p,int e)
{
    float x=p->x[e]-233,y=p->y[e]-233,r=p->radius[e];
    float cx=fabsf(x)+p->hw[e]-r,cy=fabsf(y)+p->hh[e]-r,d=hypotf(cx,cy);
    if(d+r<=230)return 0;
    float nx=copysignf(cx/d,x),ny=copysignf(cy/d,y),over=d+r-230;
    p->x[e]-=nx*over;p->y[e]-=ny*over;
    float dot=p->vx[e]*nx+p->vy[e]*ny;
    if(dot<=0)return 0;
    float bounce=dot>150?1.78f:1.f; /* resting contacts do not chatter or keep bouncing */
    p->vx[e]-=bounce*dot*nx;p->vy[e]-=bounce*dot*ny;
    if(dot>180)p->hits++;
    return dot>180?dot/900:0;
}
float pucks_update(pucks_t *p,uint32_t now,float gx,float gy)
{
    unsigned ms=now-p->ms;p->ms=now;if(ms>64)ms=64;
    float impact=0;
    while(ms) {
        unsigned step=ms>8?8:ms;ms-=step;float dt=step*.001f;
        for(int e=0;e<2;e++) {
            float ax=fabsf(gx)>.035f?fmaxf(-4,fminf(4,gx)):0,ay=fabsf(gy)>.035f?fmaxf(-4,fminf(4,gy)):0;
            p->vx[e]=(p->vx[e]+ax*7500*dt)/(1+.65f*dt);
            p->vy[e]=(p->vy[e]+ay*7500*dt)/(1+.65f*dt);
            float speed=hypotf(p->vx[e],p->vy[e]);if(speed>900){p->vx[e]*=900/speed;p->vy[e]*=900/speed;}
            p->x[e]+=p->vx[e]*dt;p->y[e]+=p->vy[e]*dt;
        }
        for(int pass=0;pass<3;pass++) {
            /* Rounded-rectangle Minkowski sum: corner contacts are rounded too. */
            float dx=p->x[1]-p->x[0],dy=p->y[1]-p->y[0],r=p->radius[0]+p->radius[1];
            float qx=fabsf(dx)-(p->hw[0]+p->hw[1]-r),qy=fabsf(dy)-(p->hh[0]+p->hh[1]-r);
            float ax=fmaxf(0,qx),ay=fmaxf(0,qy),d=hypotf(ax,ay),over=0,nx=0,ny=0;
            if(d<r) {
                if(d>.001f){nx=copysignf(ax/d,dx);ny=copysignf(ay/d,dy);over=r-d;}
                else if(qx>qy){nx=dx<0?-1:1;over=r-qx;}
                else {ny=dy<0?-1:1;over=r-qy;}
                p->x[0]-=nx*over*.5f;p->y[0]-=ny*over*.5f;p->x[1]+=nx*over*.5f;p->y[1]+=ny*over*.5f;
                float dot=(p->vx[0]-p->vx[1])*nx+(p->vy[0]-p->vy[1])*ny;
                if(dot>0){float j=(dot>150?.89f:.5f)*dot;p->vx[0]-=j*nx;p->vy[0]-=j*ny;p->vx[1]+=j*nx;p->vy[1]+=j*ny;
                    if(dot>180){impact=fmaxf(impact,dot/1200);p->hits++;}}
            }
            for(int e=0;e<2;e++)impact=fmaxf(impact,wall(p,e));
        }
    }
    if(impact>0)p->hit_ms=now;
    return fminf(1,impact);
}
void cracks_line(const gfx_band_t *b,int x,int y,int xx,int yy,uint16_t col)
{
    bool steep=abs(yy-y)>abs(xx-x);int a=steep?y:x,z=steep?yy:xx,c=steep?x:y,d=steep?xx:yy;
    if(a>z){int t=a;a=z;z=t;t=c;c=d;d=t;}if(z==a)return;
    int lo=a> (steep?b->y0:b->x0)?a:(steep?b->y0:b->x0);
    int end=steep?b->y0+b->rows:b->x0+b->w;if(z>=end)z=end-1;
    int32_t step=(int32_t)((int64_t)(d-c)*65536/((steep?abs(yy-y):abs(xx-x))));
    int32_t minor=c*65536+(lo-a)*step;
    for(int t=lo;t<=z;t++,minor+=step){int px=steep?minor/65536:t,py=steep?t:minor/65536;
        if(px>=b->x0&&px<b->x0+b->w&&py>=b->y0&&py<b->y0+b->rows)b->dst[(py-b->y0)*b->w+px-b->x0]=col;
    }
}
void cracks_paths(crack_path_t out[CRACK_RAYS])
{
    static const int dx[]={98,62,-4,-68,-99,-84,-30,37,88},dy[]={20,78,99,73,0,-54,-95,-92,-47};
    uint32_t seed=7183;
    for(int i=0;i<CRACK_RAYS;i++) {
        out[i].xy[0][0]=out[i].xy[0][1]=233;
        for(int j=1;j<=4;j++) {
            int jitter=(int)(random32(&seed)%15)-7,r=j*54;
            out[i].xy[j][0]=233+dx[i]*r/100+jitter;
            out[i].xy[j][1]=233+dy[i]*r/100-jitter;
        }
        out[i].branch[0]=out[i].xy[1][0]+dx[(i+1)%9]/3;
        out[i].branch[1]=out[i].xy[1][1]+dy[(i+1)%9]/3;
    }
}
bool cracks_in_hole(int x,int y)
{
    int dy=abs(y-233),half=46-dy/2+(dy%13)/2;
    return dy<=49 && x>=233-half && x<233+half;
}
void cracks_hole(const gfx_band_t *b,int scale)
{
    if(scale<=0)return;
    for(int y=b->y0;y<b->y0+b->rows;y++) {
        int dy=abs(y-233)*256/scale;if(dy>49)continue;
        int half=(46-dy/2+(dy%13)/2)*scale/256;
        gfx_fill(b,233-half,y,half*2,1,0);
    }
}
void cracks_paint(const gfx_band_t *b,int stage)
{
    if(stage<2)return;
    crack_path_t paths[CRACK_RAYS];cracks_paths(paths);
    const uint16_t light=gfx_rgb(135,170,180),dark=gfx_rgb(20,27,32);
    for(int pass=0;pass<3;pass++)for(int i=0;i<CRACK_RAYS;i++) {
        if((stage==2 && i%3) || (stage==3 && i%3==2))continue;
        const crack_path_t *p=&paths[i];int segments=stage==2?2:stage==3?3:4;
        if(pass==2){cracks_line(b,p->xy[1][0],p->xy[1][1],p->branch[0],p->branch[1],light);continue;}
        int off=pass==0?1:0;
        for(int j=0;j<segments;j++)cracks_line(b,p->xy[j][0]+off,p->xy[j][1]+off,p->xy[j+1][0]+off,p->xy[j+1][1]+off,pass==0?dark:light);
    }
    if(stage==4)cracks_hole(b,256);
}
