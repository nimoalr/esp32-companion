#include "play.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
bool petting_update(petting_t *p, bool down, float x, float y, uint32_t now)
{
    if (!down || x<20 || x>446 || y<0 || y>185) {p->down=false;p->travel=0;return false;}
    if (!p->down || now-p->since>2200) {
        p->down=true;p->x=p->span_x=x;p->y=p->span_y=y;p->travel=0;p->since=now;return false;
    }
    float distance=fabsf(x-p->x)+fabsf(y-p->y);p->x=x;p->y=y;
    if(distance>55){p->down=false;return false;} /* discontinuous controller sample */
    if(distance>1.5f)p->travel+=distance;
    if(p->travel>=36 && fabsf(x-p->span_x)+fabsf(y-p->span_y)>=22 && now-p->last>=220) {
        p->count++;p->last=p->since=now;p->travel=0;p->span_x=x;p->span_y=y;return true;
    }
    return false;
}
static uint32_t random32(uint32_t *r){*r=*r*1664525u+1013904223u;return *r;}
void pucks_init(pucks_t *p,uint32_t now,uint32_t seed)
{
    memset(p,0,sizeof *p);p->ms=now;
    for(int e=0;e<2;e++) {
        p->x[e]=138+190*e;p->y[e]=233;
        p->vx[e]=(e?-1:1)*(190+random32(&seed)%90);
        p->vy[e]=(e?1:-1)*(160+random32(&seed)%110);
    }
}
float pucks_update(pucks_t *p,uint32_t now,float gx,float gy)
{
    unsigned ms=now-p->ms;p->ms=now;if(ms>64)ms=64;
    float impact=0;
    while(ms) {
        unsigned step=ms>8?8:ms;ms-=step;float dt=step*.001f;
        for(int e=0;e<2;e++) {
            p->vx[e]+=gx*160*dt;p->vy[e]+=gy*160*dt;
            float speed=hypotf(p->vx[e],p->vy[e]);if(speed>430){p->vx[e]*=430/speed;p->vy[e]*=430/speed;}
            p->x[e]+=p->vx[e]*dt;p->y[e]+=p->vy[e]*dt;
            float x=p->x[e]-233,y=p->y[e]-233,d=hypotf(x,y);
            if(d>201) {
                x/=d;y/=d;p->x[e]=233+x*201;p->y[e]=233+y*201;
                float dot=p->vx[e]*x+p->vy[e]*y;
                if(dot>0){p->vx[e]-=1.96f*dot*x;p->vy[e]-=1.96f*dot*y;impact=fmaxf(impact,dot/400);p->hits++;}
            }
        }
        float dx=p->x[1]-p->x[0],dy=p->y[1]-p->y[0],d=hypotf(dx,dy);
        if(d<58 && d>.01f){
            dx/=d;dy/=d;float overlap=(58-d)*.5f;
            p->x[0]-=dx*overlap;p->y[0]-=dy*overlap;p->x[1]+=dx*overlap;p->y[1]+=dy*overlap;
            float dot=(p->vx[0]-p->vx[1])*dx+(p->vy[0]-p->vy[1])*dy;
            if(dot>0){p->vx[0]-=dot*dx;p->vy[0]-=dot*dy;p->vx[1]+=dot*dx;p->vy[1]+=dot*dy;impact=fmaxf(impact,dot/500);p->hits++;}
        }
    }
    if(impact>0)p->hit_ms=now;
    return fminf(1,impact);
}
static void crack_line(const gfx_band_t *b,int x,int y,int xx,int yy,uint16_t col)
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
void cracks_paint(const gfx_band_t *b,int stage)
{
    if(stage<2)return;
    static const int dx[]={98,62,-4,-68,-99,-84,-30,37,88},dy[]={20,78,99,73,0,-54,-95,-92,-47};
    uint32_t seed=7183;const int rays=stage==2?3:stage==3?6:9;
    const uint16_t light=gfx_rgb(135,170,180),dark=gfx_rgb(20,27,32);
    for(int i=0;i<rays;i++) {
        int x=233,y=233,axis=i*9/rays;
        for(int j=1;j<=4;j++) {
            int jitter=(int)(random32(&seed)%15)-7,r=j*(stage==2?24:stage==3?42:54);
            int xx=233+dx[axis]*r/100+jitter,yy=233+dy[axis]*r/100-jitter;
            crack_line(b,x+1,y+1,xx+1,yy+1,dark);crack_line(b,x,y,xx,yy,light);
            if(j==2)crack_line(b,x,y,x+dx[(axis+1)%9]/3,y+dy[(axis+1)%9]/3,light);
            x=xx;y=yy;
        }
    }
    if(stage==4)for(int yy=b->y0;yy<b->y0+b->rows;yy++) {
        int dy=abs(yy-233);if(dy>49)continue;int half=46-dy/2+(dy%13)/2;
        gfx_fill(b,233-half,yy,half*2,1,0);
    }
}
