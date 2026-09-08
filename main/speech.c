#include "speech.h"

#include <string.h>
#include <stdatomic.h>
#ifndef SPEECH_HOST_TEST
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#endif
#include "esp_log.h"
#include "adpcm.h"
#include "audio.h"

static const char *TAG = "speech";

#define BLOCK 160               /* 10 ms */
#define PA_LEAD_MS 8            /* silence before the sound with the amplifier already on */
#define PA_TAIL_MS 120          /* silence after, then the amplifier goes off */

typedef enum { REQ_GESTURE, REQ_BABBLE, REQ_WORD, REQ_EFFECT } req_kind_t;
typedef struct {
    req_kind_t kind;
    int id;
    float level, energy;
    unsigned epoch,purr_epoch;
} req_t;

static QueueHandle_t s_q, s_effect_q;
static sfx_t s_effects;
typedef struct {sfx_id_t id;float level;} effect_req_t;
static volatile bool s_busy;
static atomic_bool s_purring;
static atomic_uint s_epoch,s_purr_epoch;
static voice_t s_voice;
static voice_register_t s_reg = VOICE_REG_HIGH;
static int16_t s_block[BLOCK];

static bool wait_audio(void)
{
    /* the render loop starts the audio when it sees speech_busy(); give it a moment */
    for (int i = 0; i < 60; i++) {
        if (audio_running()) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static void mix_effects(int16_t *pcm,int n)
{
    effect_req_t e;
    while(xQueueReceive(s_effect_q,&e,0)==pdTRUE)sfx_start(&s_effects,e.id,e.level);
    if(sfx_active(&s_effects))sfx_mix(&s_effects,pcm,n);
}
void speech_effect(sfx_id_t id,float level)
{
    if(!s_effect_q || !s_q)return;
    effect_req_t e={id,level};if(xQueueSend(s_effect_q,&e,0)!=pdTRUE)return;
    const req_t wake={.kind=REQ_EFFECT};xQueueSend(s_q,&wake,0);
}

static bool canceled(const req_t *r)
{
    return r->epoch!=atomic_load(&s_epoch) ||
        (r->kind==REQ_GESTURE && r->id==VOICE_PURR && r->purr_epoch!=atomic_load(&s_purr_epoch));
}
void speech_cancel_purr(void){atomic_fetch_add(&s_purr_epoch,1);atomic_store(&s_purring,false);}
void speech_cancel_voice(void){atomic_fetch_add(&s_epoch,1);atomic_store(&s_purring,false);}

/* One 10 ms ramp from the last emitted sample; cancellation does not wait for
 * a looping gesture or a word to finish and does not cut at arbitrary amplitude. */
static void release_voice(void)
{
    int last=s_block[BLOCK-1];
    for(int i=0;i<BLOCK;i++)s_block[i]=(int16_t)(last*(BLOCK-1-i)/BLOCK);
    mix_effects(s_block,BLOCK);audio_write(s_block,BLOCK);
}

static void play_silence(int ms)
{
    memset(s_block, 0, sizeof s_block);
    for (int i = 0; i < (ms+9) / 10; i++) audio_write(s_block, BLOCK);
}

/* I2S has 6 x 240 frames = 90 ms queued at 16 kHz. Keep the PA on
 * through the drain, including effects arriving during another sound's tail. */
static void drain_sound(void)
{
    int quiet=0,guard=300;
    while(quiet<PA_TAIL_MS/10 && guard--) {
        bool playing=sfx_active(&s_effects)||uxQueueMessagesWaiting(s_effect_q);
        memset(s_block,0,sizeof s_block);mix_effects(s_block,BLOCK);
        if(audio_write(s_block,BLOCK)!=ESP_OK)break;
        quiet=playing?0:quiet+1;
    }
}

static void say(const req_t *r)
{
    if(r->kind!=REQ_EFFECT && canceled(r))return;
    if (!wait_audio()) {
        ESP_LOGW(TAG, "no audio, dropped");
        return;
    }
    if(r->kind!=REQ_EFFECT && canceled(r))return;
    audio_set_muted(true);
    audio_pa(true);
    play_silence(PA_LEAD_MS);
    const int64_t t0 = esp_timer_get_time();
    memset(s_block,0,sizeof s_block);
    atomic_store_explicit(&s_purring, r->kind == REQ_GESTURE && r->id == VOICE_PURR, memory_order_relaxed);
    if (r->kind == REQ_WORD) {
        const clip_t *c = &k_clips[r->id];
        adpcm_state_t st = { 0, 0 };
        const float gain = r->level < 0.f ? 0.f : r->level > 1.f ? 1.f : r->level;
        for (uint32_t pos = 0; pos < c->samples; pos += BLOCK) {
            if(canceled(r)){release_voice();break;}
            const int n = (int)(c->samples - pos < BLOCK ? c->samples - pos : BLOCK);
            adpcm_decode(&st, c->data + pos / 2, s_block, n & ~1);
            if (n & 1) s_block[n - 1] = 0;
            if (gain < 1.f) for (int i = 0; i < n; i++) s_block[i] = (int16_t)(s_block[i] * gain);
            if (n < BLOCK) memset(s_block + n, 0, sizeof(int16_t) * (size_t)(BLOCK - n));
            mix_effects(s_block,BLOCK);
            if (audio_write(s_block, BLOCK) != ESP_OK) break;
        }
    } else if(r->kind==REQ_EFFECT) {
        int guard=100; /* at most one second before checking pending voice requests */
        do {
            memset(s_block,0,sizeof s_block);mix_effects(s_block,BLOCK);
            if(audio_write(s_block,BLOCK)!=ESP_OK)break;
        } while(guard-- && (sfx_active(&s_effects)||uxQueueMessagesWaiting(s_effect_q)));
    } else {
        voice_set_register(&s_voice, s_reg);
        if (r->kind == REQ_GESTURE) voice_start(&s_voice, (voice_id_t)r->id, r->level);
        else voice_babble(&s_voice, r->level, r->energy);
        int guard = 500;    /* 5 s: a looping gesture (purr) needs voice_stop; here it is cut */
        while (voice_active(&s_voice) && guard--) {
            if(canceled(r)){release_voice();voice_stop(&s_voice);break;}
            voice_render(&s_voice, s_block, BLOCK);
            mix_effects(s_block,BLOCK);
            if (audio_write(s_block, BLOCK) != ESP_OK) break;
            if (guard == 30) voice_stop(&s_voice);
        }
    }
    atomic_store_explicit(&s_purring, false, memory_order_relaxed);
    drain_sound();
    audio_pa(false);
    audio_set_muted(false);
    ESP_LOGI(TAG, "said %s (%lld ms)", r->kind == REQ_WORD ? k_clips[r->id].name : r->kind == REQ_GESTURE ? k_voice_gestures[r->id].name : r->kind==REQ_EFFECT?"effect":"babble",
             (esp_timer_get_time() - t0) / 1000);
}

static void speech_task(void *arg)
{
    req_t r;
    for (;;) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) continue;
        if(r.kind==REQ_EFFECT && !uxQueueMessagesWaiting(s_effect_q) && !sfx_active(&s_effects))continue;
        s_busy = true;
        say(&r);
        s_busy = false;
    }
}

esp_err_t speech_init(void)
{
    voice_init(&s_voice, (uint32_t)esp_timer_get_time());
    s_effect_q=xQueueCreate(8,sizeof(effect_req_t)); /* bonk + crack in the same frame must both survive */
    s_q = xQueueCreate(2, sizeof(req_t));
    if (!s_q || !s_effect_q) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(speech_task, "speech", 6144, NULL, 5, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static bool post(const req_t *r, bool interrupt)
{
    if (!s_q) return false;
    if (s_busy && !interrupt) return false;
    if (interrupt) {atomic_fetch_add(&s_epoch,1);xQueueReset(s_q);}
    req_t queued=*r;queued.epoch=atomic_load(&s_epoch);queued.purr_epoch=atomic_load(&s_purr_epoch);
    return xQueueSend(s_q, &queued, 0) == pdTRUE;
}

bool speech_gesture(voice_id_t id, float level, bool interrupt)
{
    const req_t r = { .kind=REQ_GESTURE, .id=(int)id, .level=level };
    return post(&r, interrupt);
}

bool speech_babble(float level, float energy)
{
    const req_t r = { .kind=REQ_BABBLE, .level=level, .energy=energy };
    return post(&r, false);
}

bool speech_word(int clip, float level, bool interrupt)
{
    if (clip < 0 || clip >= k_clips_n) return false;
    const req_t r = { .kind=REQ_WORD, .id=clip, .level=level };
    return post(&r, interrupt);
}

bool speech_busy(void)
{
    return s_busy || (s_q && uxQueueMessagesWaiting(s_q) > 0) || (s_effect_q && uxQueueMessagesWaiting(s_effect_q));
}

bool speech_purring(void)
{
    return atomic_load_explicit(&s_purring, memory_order_relaxed);
}

void speech_set_register(voice_register_t reg)
{
    s_reg = reg;
}

void speech_set_volume(int pct)
{
    audio_set_volume(pct);
}
