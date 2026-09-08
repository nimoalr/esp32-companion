/* Execute the real speech loop with deterministic queue/audio I/O stand-ins. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "speech.h"
typedef struct {unsigned size,cap,n;unsigned char data[16][128];} queue_t;
typedef queue_t *QueueHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERR_NO_MEM 0x101
static QueueHandle_t xQueueCreate(unsigned cap,unsigned size){assert(cap<=16&&size<=128);queue_t *q=calloc(1,sizeof *q);q->cap=cap;q->size=size;return q;}
static int xQueueSend(QueueHandle_t q,const void *v,unsigned ticks){(void)ticks;if(q->n==q->cap)return 0;memcpy(q->data[q->n++],v,q->size);return 1;}
static int xQueueReceive(QueueHandle_t q,void *v,unsigned ticks){(void)ticks;if(!q->n)return 0;memcpy(v,q->data[0],q->size);q->n--;memmove(q->data,q->data+1,q->n*128);return 1;}
static void xQueueReset(QueueHandle_t q){q->n=0;}
static unsigned uxQueueMessagesWaiting(QueueHandle_t q){return q->n;}
static int xTaskCreatePinnedToCore(void (*f)(void*),const char *n,unsigned stack,void *arg,int pri,void *handle,int core){return 1;}
static void vTaskDelay(unsigned ms){}
static int64_t esp_timer_get_time(void){return 1000;}
#define SPEECH_HOST_TEST 1
#include "../../main/speech.c"
static unsigned writes,last_nonzero;static int action;static bool pa;
bool audio_running(void){return true;}
void audio_set_muted(bool on){}
void audio_pa(bool on){if(!on)assert(writes-last_nonzero>=9);pa=on;}
void audio_set_volume(int n){}
esp_err_t audio_write(const int16_t *p,int n)
{
    assert(n==160&&pa);writes++;
    for(int i=0;i<n;i++)if(p[i])last_nonzero=writes;
    if(writes==4){if(action==1)speech_cancel_purr();else if(action==2)assert(speech_word(0,.5f,true));}
    if(writes==5&&action==3)speech_effect(SFX_GLASS,1);
    return ESP_OK;
}
static req_t take(void){req_t r;assert(xQueueReceive(s_q,&r,0));return r;}
int main(void)
{
    assert(speech_init()==ESP_OK);
    assert(speech_gesture(VOICE_PURR,.8f,false));req_t r=take();action=1;s_busy=true;say(&r);s_busy=false;
    assert(writes==17&&!speech_purring());
    writes=last_nonzero=0;assert(speech_gesture(VOICE_PURR,.8f,false));r=take();speech_cancel_purr();say(&r);assert(!writes);
    action=2;assert(speech_word(0,.8f,false));r=take();s_busy=true;say(&r);s_busy=false;
    assert(writes==17);req_t next=take();assert(!canceled(&next));
    speech_effect(SFX_BONK,1);speech_effect(SFX_GLASS,1);assert(uxQueueMessagesWaiting(s_effect_q)==2);
    int16_t pcm[160]={0};mix_effects(pcm,160);assert(sfx_active(&s_effects)&&s_effects.cursor==2);
    writes=last_nonzero=0;action=0;speech_cancel_purr();say(&next);assert(writes>7); /* purr cancellation leaves words intact */
    writes=last_nonzero=0;action=3;speech_effect(SFX_BONK,1);req_t effect={.kind=REQ_EFFECT};say(&effect);
    assert(writes>=50&&!sfx_active(&s_effects)&&!uxQueueMessagesWaiting(s_effect_q));
    puts("PASS drained I2S before amplifier-off, effects arriving during playback, active purr and word interruption at next block, queued purr invalidation, intact replacement word, simultaneous bonk + crack");
}
