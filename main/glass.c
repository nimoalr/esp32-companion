#include "glass.h"
#include "play.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
static void mark(glass_t *g,int x,int y)
{
    for(int yy=y-1;yy<=y+1;yy++)for(int xx=x-1;xx<=x+1;xx++)
        if((unsigned)xx<466&&(unsigned)yy<466)g->dirty[yy/32]|=1u<<(xx/32);
}
static void edge(glass_t *g,int x,int y,int xx,int yy)
{
    int n=abs(xx-x)>abs(yy-y)?abs(xx-x):abs(yy-y);
    if(!n){mark(g,x,y);return;}
    for(int i=0;i<=n;i++)mark(g,x+(xx-x)*i/n,y+(yy-y)*i/n);
}
static void damage(glass_t *g,const glass_piece_t *p)
{
    if(!p->visible)return;
    for(int j=0;j<GLASS_POINTS;j++)edge(g,p->xy[j][0],p->xy[j][1],p->xy[(j+1)%GLASS_POINTS][0],p->xy[(j+1)%GLASS_POINTS][1]);
    edge(g,p->branch[0][0],p->branch[0][1],p->branch[1][0],p->branch[1][1]);
}
static void outside_hole(int *x,int *y)
{
    int dx=*x-233,dy=*y-233;
    for(int i=1;i<=256;i++) {
        int xx=233+dx*i/256,yy=233+dy*i/256;
        if(!cracks_in_hole(xx,yy)){*x=xx;*y=yy;return;}
    }
}
static void shape(glass_piece_t *p,const crack_path_t *paths,int i,float t)
{
    const crack_path_t *a=&paths[i],*b=&paths[(i+1)%9];
    for(int j=0;j<5;j++)for(int k=0;k<2;k++){p->xy[j][k]=a->xy[j][k];p->xy[6+j][k]=b->xy[4-j][k];}
    float mx=(a->xy[4][0]+b->xy[4][0])*.5f-233,my=(a->xy[4][1]+b->xy[4][1])*.5f-233;
    float r=hypotf(mx,my);
    p->xy[5][0]=233+(int)lroundf(mx*231/r);p->xy[5][1]=233+(int)lroundf(my*231/r);
    p->xy[11][0]=233+(int)mx;p->xy[11][1]=233+(int)my;
    outside_hole(&p->xy[11][0],&p->xy[11][1]);
    for(int k=0;k<2;k++){p->branch[0][k]=a->xy[1][k];p->branch[1][k]=a->branch[k];}
    p->visible=true;p->moving=t>0;
    if(!p->moving)return;
    /* Cut out the very same central hole before moving the existing region. */
    p->xy[0][0]=a->xy[1][0];p->xy[0][1]=a->xy[1][1];outside_hole(&p->xy[0][0],&p->xy[0][1]);
    p->xy[10][0]=b->xy[1][0];p->xy[10][1]=b->xy[1][1];outside_hole(&p->xy[10][0],&p->xy[10][1]);
    float cx=233+mx*.6f,cy=233+my*.6f,spin=(i&1?1:-1)*t*.32f,cs=cosf(spin),sn=sinf(spin);
    float dx=(i-4)*9*t,dy=65*t+650*t*t;
    for(int j=0;j<GLASS_POINTS+2;j++) {
        int *v=j<GLASS_POINTS?p->xy[j]:p->branch[j-GLASS_POINTS];
        float x=v[0]-cx,y=v[1]-cy;
        v[0]=(int)lroundf(cx+x*cs-y*sn+dx);v[1]=(int)lroundf(cy+x*sn+y*cs+dy);
    }
}
int glass_update(glass_t *g,int stage,uint32_t age,uint32_t now,glass_rect_t out[GLASS_MAX_DIRTY])
{
    uint32_t bucket=(uint64_t)now*30/1000;
    if(stage==g->stage && (stage!=4 || bucket==g->bucket))return 0;
    int previous=g->stage;g->bucket=bucket;memset(g->dirty,0,sizeof g->dirty);
    static const int rank[9]={4,0,7,2,8,1,6,3,5};
    unsigned released=0;crack_path_t paths[CRACK_RAYS];cracks_paths(paths);
    for(int i=0;i<GLASS_SHARDS;i++) {
        glass_piece_t p={0};
        if(stage==4) {
            int elapsed=(int)age-(900+rank[i]*500);
            if(elapsed>0)released++;
            if(elapsed<1100)shape(&p,paths,i,elapsed>0?elapsed*.001f:0);
        }
        if(memcmp(&p,&g->piece[i],sizeof p)){damage(g,&g->piece[i]);damage(g,&p);g->piece[i]=p;}
    }
    int hole=stage==4?(age<6000?256:age<6400?(int)(256*(6400-age)/400):0):0;
    if(hole!=g->hole)for(int y=176;y<292;y+=16)for(int x=176;x<292;x+=16)mark(g,x,y);
    g->hole=hole;g->released=released;g->stage=stage;
    /* A stage change replaces the static crack overlay too. */
    if(previous!=stage){out[0]=(glass_rect_t){0,0,466,466};return 1;}
    int n=0;
    for(int y=0;y<15;y++)for(int x=0;x<15;) {
        if(!(g->dirty[y]&(1u<<x))){x++;continue;}
        int start=x;while(x<15&&(g->dirty[y]&(1u<<x)))x++;
        out[n++]=(glass_rect_t){start*32,y*32,x*32<466?x*32:466,(y+1)*32<466?(y+1)*32:466};
    }
    return n;
}
void glass_paint(const glass_t *g,const gfx_band_t *b)
{
    if(g->stage!=4){cracks_paint(b,g->stage);return;}
    uint16_t light=gfx_rgb(135,170,180),dark=gfx_rgb(20,27,32);
    for(int pass=0;pass<3;pass++)for(int i=0;i<GLASS_SHARDS;i++)if(g->piece[i].visible) {
        const glass_piece_t *p=&g->piece[i];
        if(pass==2){cracks_line(b,p->branch[0][0],p->branch[0][1],p->branch[1][0],p->branch[1][1],light);continue;}
        for(int j=0;j<GLASS_POINTS;j++) {
            /* At rest, only existing fractures are visible. The rim and hole
             * already delimit the region; their edges appear as it falls. */
            if(!p->moving && (j==4 || j==5 || j==10 || j==11))continue;
            const int *v=p->xy[j],*w=p->xy[(j+1)%GLASS_POINTS];
            int off=pass==0?1:0;
            cracks_line(b,v[0]+off,v[1]+off,w[0]+off,w[1]+off,pass==0?dark:light);
        }
    }
    cracks_hole(b,g->hole);
}
