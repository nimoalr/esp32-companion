#include "speech.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "adpcm.h"
#include "audio.h"

static const char *TAG = "speech";

#define BLOCK 160               /* 10 ms */
#define PA_LEAD_MS 8            /* silence before the sound with the amplifier already on */
#define PA_TAIL_MS 120          /* silence after, then the amplifier goes off */

typedef enum { REQ_GESTURE, REQ_BABBLE, REQ_WORD } req_kind_t;
typedef struct {
    req_kind_t kind;
    int id;
    float level, energy;
} req_t;

static QueueHandle_t s_q;
static volatile bool s_busy;
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

static void play_silence(int ms)
{
    memset(s_block, 0, sizeof s_block);
    for (int i = 0; i < ms / 10; i++) audio_write(s_block, BLOCK);
}

static void say(const req_t *r)
{
    if (!wait_audio()) {
        ESP_LOGW(TAG, "no audio, dropped");
        return;
    }
    audio_set_muted(true);
    audio_pa(true);
    play_silence(PA_LEAD_MS);
    const int64_t t0 = esp_timer_get_time();
    if (r->kind == REQ_WORD) {
        const clip_t *c = &k_clips[r->id];
        adpcm_state_t st = { 0, 0 };
        const float gain = r->level < 0.f ? 0.f : r->level > 1.f ? 1.f : r->level;
        for (uint32_t pos = 0; pos < c->samples; pos += BLOCK) {
            const int n = (int)(c->samples - pos < BLOCK ? c->samples - pos : BLOCK);
            adpcm_decode(&st, c->data + pos / 2, s_block, n & ~1);
            if (n & 1) s_block[n - 1] = 0;
            if (gain < 1.f) for (int i = 0; i < n; i++) s_block[i] = (int16_t)(s_block[i] * gain);
            if (n < BLOCK) memset(s_block + n, 0, sizeof(int16_t) * (size_t)(BLOCK - n));
            if (audio_write(s_block, BLOCK) != ESP_OK) break;
        }
    } else {
        voice_set_register(&s_voice, s_reg);
        if (r->kind == REQ_GESTURE) voice_start(&s_voice, (voice_id_t)r->id, r->level);
        else voice_babble(&s_voice, r->level, r->energy);
        int guard = 500;    /* 5 s: a looping gesture (purr) needs voice_stop; here it is cut */
        while (voice_active(&s_voice) && guard--) {
            voice_render(&s_voice, s_block, BLOCK);
            if (audio_write(s_block, BLOCK) != ESP_OK) break;
            if (guard == 30) voice_stop(&s_voice);
        }
    }
    play_silence(PA_TAIL_MS);
    audio_pa(false);
    audio_set_muted(false);
    ESP_LOGI(TAG, "said %s (%lld ms)", r->kind == REQ_WORD ? k_clips[r->id].name : r->kind == REQ_GESTURE ? k_voice_gestures[r->id].name : "babble",
             (esp_timer_get_time() - t0) / 1000);
}

static void speech_task(void *arg)
{
    req_t r;
    for (;;) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) continue;
        s_busy = true;
        say(&r);
        s_busy = false;
    }
}

esp_err_t speech_init(void)
{
    voice_init(&s_voice, (uint32_t)esp_timer_get_time());
    s_q = xQueueCreate(2, sizeof(req_t));
    if (!s_q) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(speech_task, "speech", 6144, NULL, 5, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static bool post(const req_t *r, bool interrupt)
{
    if (s_busy && !interrupt) return false;
    if (interrupt) xQueueReset(s_q);
    return xQueueSend(s_q, r, 0) == pdTRUE;
}

bool speech_gesture(voice_id_t id, float level, bool interrupt)
{
    const req_t r = { REQ_GESTURE, (int)id, level, 0.f };
    return post(&r, interrupt);
}

bool speech_babble(float level, float energy)
{
    const req_t r = { REQ_BABBLE, 0, level, energy };
    return post(&r, false);
}

bool speech_word(int clip, float level, bool interrupt)
{
    if (clip < 0 || clip >= k_clips_n) return false;
    const req_t r = { REQ_WORD, clip, level, 0.f };
    return post(&r, interrupt);
}

bool speech_busy(void)
{
    return s_busy || (s_q && uxQueueMessagesWaiting(s_q) > 0);
}

void speech_set_register(voice_register_t reg)
{
    s_reg = reg;
}

void speech_set_volume(int pct)
{
    audio_set_volume(pct);
}
