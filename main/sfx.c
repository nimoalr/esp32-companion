#include "sfx.h"
#include <string.h>
#include "sfx_samples.h"
void sfx_start(sfx_t *s,sfx_id_t id,float level)
{
    if(id<=SFX_NONE||id>SFX_TINKLE)return;
    sfx_note_t *v=&s->note[s->cursor++%3];memset(v,0,sizeof *v);
    float gain=level<0?0:level>1?1:level;
    if(id==SFX_BONK || id==SFX_GLASS || id==SFX_SHATTER || id==SFX_TINKLE) {
        v->data=id==SFX_BONK?sfx_bonk:id==SFX_GLASS?sfx_crack:id==SFX_SHATTER?sfx_shatter:sfx_tinkle;
        v->left=v->total=2*(id==SFX_BONK?sizeof sfx_bonk:id==SFX_GLASS?sizeof sfx_crack:id==SFX_SHATTER?sizeof sfx_shatter:sizeof sfx_tinkle);
        v->gain=(int)(32768*gain);v->id=id;return;
    }
    static const int hz[]={0,1200,850,1700,520,2300,920},len[]={0,640,240,1200,1600,2400,2240};
    v->step=(uint32_t)((uint64_t)hz[id]*4294967296ULL/16000);v->left=v->total=len[id];
    v->gain=(int)(2200*(level<0?0:level>1?1:level));v->id=id;v->noise=17+s->cursor*103;
}
bool sfx_active(const sfx_t *s){for(int i=0;i<3;i++)if(s->note[i].left)return true;return false;}
void sfx_mix(sfx_t *s,int16_t *pcm,int n)
{
    for(int k=0;k<n;k++) {
        int sum=pcm[k];
        for(int i=0;i<3;i++) {
            sfx_note_t *v=&s->note[i];if(!v->left)continue;
            if(v->data) {
                int pos=v->total-v->left;
                if(!(pos&1))adpcm_decode(&v->adpcm,v->data+pos/2,v->pair,2);
                sum+=(int)((int32_t)v->pair[pos&1]*v->gain/32768);v->left--;continue;
            }
            v->phase+=v->step;int p=(int)(v->phase>>16),tri=p<32768?p*2-32768:98303-p*2;
            int age=v->total-v->left,env=v->left*256/v->total;if(age<32)env=env*age/32;
            sum+=(int)((int64_t)tri*v->gain*env/(32768*256));v->left--;
        }
        pcm[k]=(int16_t)(sum>32767?32767:sum< -32768?-32768:sum);
    }
}
