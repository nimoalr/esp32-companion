/*
 * Companion eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.75.
 *
 *   core 0: app_main (init), touch task (CST9217 -> tap queue)
 *   core 1: render task (power state machine -> animation -> dirty rects ->
 *           band rasteriser -> QSPI DMA)
 */
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "board.h"
#include "display.h"
#include "touch.h"
#include "raster.h"
#include "eyes.h"
#include "anim.h"
#include "power.h"
#include "settings.h"
#include "i2c_bus.h"

static const char *TAG = "eyes";

#define VSYNC_TIMEOUT_MS   40
#define STATS_PERIOD_US    1000000
#define DROWSY_PERIOD      (60 / CONFIG_EYES_DROWSY_FPS)

typedef struct {
    int x0, y0, x1, y1;     /* [x0, x1) x [y0, y1) in pixels; empty when x0 >= x1 */
} rect_t;

static QueueHandle_t s_tap_q;

static inline uint32_t ms_now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---- rectangles ---------------------------------------------------------- */

static inline rect_t rect_empty(void)
{
    return (rect_t){ 0, 0, 0, 0 };
}

static inline bool rect_is_empty(const rect_t *r)
{
    return r->x0 >= r->x1 || r->y0 >= r->y1;
}

static rect_t rect_union(const rect_t *a, const rect_t *b)
{
    if (rect_is_empty(a)) return *b;
    if (rect_is_empty(b)) return *a;
    return (rect_t){
        a->x0 < b->x0 ? a->x0 : b->x0,
        a->y0 < b->y0 ? a->y0 : b->y0,
        a->x1 > b->x1 ? a->x1 : b->x1,
        a->y1 > b->y1 ? a->y1 : b->y1,
    };
}

static bool rect_overlaps(const rect_t *a, const rect_t *b)
{
    return !rect_is_empty(a) && !rect_is_empty(b) &&
           a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

/* Grow to the panel's 2-pixel address granularity and clip to the screen. */
static rect_t rect_align(rect_t r)
{
    if (rect_is_empty(&r)) return rect_empty();
    r.x0 &= ~(BOARD_LCD_PIXEL_ALIGN - 1);
    r.y0 &= ~(BOARD_LCD_PIXEL_ALIGN - 1);
    r.x1 = (r.x1 + BOARD_LCD_PIXEL_ALIGN - 1) & ~(BOARD_LCD_PIXEL_ALIGN - 1);
    r.y1 = (r.y1 + BOARD_LCD_PIXEL_ALIGN - 1) & ~(BOARD_LCD_PIXEL_ALIGN - 1);
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > DISPLAY_W) r.x1 = DISPLAY_W;
    if (r.y1 > DISPLAY_H) r.y1 = DISPLAY_H;
    return r;
}

static rect_t shape_rect(const raster_shape_t *s)
{
    if (!s->visible) return rect_empty();
    return (rect_t){ s->px0, s->py0, s->px1, s->py1 };
}

/* Rasterise and push one rect in bands of DISPLAY_BAND_ROWS rows. */
static void push_rect(const rect_t *r, const raster_shape_t *shapes)
{
    const int w = r->x1 - r->x0;
    for (int y = r->y0; y < r->y1; y += DISPLAY_BAND_ROWS) {
        int rows = r->y1 - y;
        if (rows > DISPLAY_BAND_ROWS) rows = DISPLAY_BAND_ROWS;
        uint16_t *band = display_acquire_band();
        raster_band(band, r->x0, y, w, rows, shapes, 2);
        display_push(r->x0, y, w, rows, band);
    }
}

/* ---- brightness fade ------------------------------------------------------ */

static int s_bri_cur = -1;     /* last value sent to the panel */
static int s_bri_target;

static void brightness_target(int pct)
{
    s_bri_target = pct;
}

/* Move one step toward the target; `step` percent per frame. */
static void brightness_step(int step)
{
    if (s_bri_cur == s_bri_target) return;
    int v = s_bri_cur < 0 ? s_bri_target : s_bri_cur;
    if (v < s_bri_target) {
        v += step;
        if (v > s_bri_target) v = s_bri_target;
    } else {
        v -= step;
        if (v < s_bri_target) v = s_bri_target;
    }
    s_bri_cur = v;
    display_set_brightness((uint8_t)v);
}

static void brightness_set_now(int pct)
{
    s_bri_target = pct;
    s_bri_cur = pct;
    display_set_brightness((uint8_t)pct);
}

/* ---- render task ----------------------------------------------------------- */

typedef struct {
    eyes_t eyes;
    anim_sm_t sm;
    raster_shape_t shapes[2];
    rect_t prev[2];
    anim_id_t saved_anim;      /* expression to restore after DROWSY */
} render_ctx_t;

static void drain_taps(void)
{
    tap_event_t tap;
    while (xQueueReceive(s_tap_q, &tap, 0) == pdTRUE) {
    }
}

static void eyes_closed_now(render_ctx_t *c, uint32_t now_ms)
{
    const eye_pose_t closed = { { Q16_ONE, Q16(0.03), 0, 0, 0, 0, 0, 0 } };
    eyes_set_target(&c->eyes, 0, &closed, 1, now_ms);
    eyes_set_target(&c->eyes, 1, &closed, 1, now_ms);
    eyes_update(&c->eyes, now_ms + 2, c->shapes);   /* settle the closed pose */
}

/* SLEEP: panel off, light sleep until motion/touch (-> ACTIVE) or the deadline (-> DEEP). */
static void do_sleep(render_ctx_t *c)
{
    brightness_set_now(0);
    display_sleep(true);
    const uint32_t entered = ms_now();

    for (;;) {
        const uint32_t elapsed = ms_now() - entered;
        const uint32_t budget = CONFIG_EYES_SLEEP_TO_DEEP_S * 1000u;
        if (elapsed >= budget) {
            power_enter_deep();
        }
        const power_wake_t w = power_light_sleep(budget - elapsed);
        if (w == POWER_WAKE_TIMEOUT) {
            power_enter_deep();
        }
        if (w == POWER_WAKE_MOTION || w == POWER_WAKE_TOUCH) {
            break;
        }
    }

    /* Wake: eyes closed, panel back on black, then ease open at full brightness. */
    const uint32_t now_ms = ms_now();
    power_wake_to_active(now_ms);
    eyes_closed_now(c, now_ms);
    c->prev[0] = c->prev[1] = rect_empty();
    display_sleep(false);
    anim_set(&c->sm, &c->eyes, ANIM_NEUTRAL, ms_now());
    brightness_set_now(g_settings.brightness_active);
    drain_taps();
}

static void on_transition(render_ctx_t *c, power_state_t from, power_state_t to, uint32_t now_ms)
{
    switch (to) {
    case POWER_DROWSY:
        c->saved_anim = c->sm.id;
        anim_set(&c->sm, &c->eyes, ANIM_SLEEPY, now_ms);
        brightness_target(g_settings.brightness_aod);
        break;
    case POWER_ACTIVE:
        if (from == POWER_DROWSY) {
            anim_set(&c->sm, &c->eyes, c->saved_anim, now_ms);
            brightness_target(g_settings.brightness_active);
            drain_taps();      /* the touch that woke us is not an expression change */
        }
        break;
    default:
        break;
    }
}

static void render_task(void *arg)
{
    static render_ctx_t c;
    uint32_t now_ms = ms_now();
    eyes_init(&c.eyes, now_ms);
    anim_init(&c.sm, &c.eyes, now_ms);
    c.prev[0] = c.prev[1] = rect_empty();
    c.saved_anim = ANIM_NEUTRAL;

    display_fill_black();
    brightness_set_now(g_settings.brightness_active);

    power_state_t state = power_state();

    /* stats */
    int64_t stats_t0 = esp_timer_get_time();
    uint32_t frames = 0, vsync_miss = 0;
    uint64_t frame_us_sum = 0;
    uint32_t frame_us_max = 0;
    uint32_t rects_sum = 0;

    for (;;) {
        now_ms = ms_now();

        /* Taps -> next expression (ACTIVE only; in DROWSY the touch itself wakes us). */
        tap_event_t tap;
        while (xQueueReceive(s_tap_q, &tap, 0) == pdTRUE) {
            if (state == POWER_ACTIVE) {
                anim_next(&c.sm, &c.eyes, now_ms);
            }
        }

        /* Power state machine. */
        const power_state_t next = power_update(now_ms, touch_last_activity_ms());
        if (next != state) {
            on_transition(&c, state, next, now_ms);
            state = next;
        }
        if (state == POWER_SLEEP) {
            do_sleep(&c);
            state = power_state();
            stats_t0 = esp_timer_get_time();
            frames = 0;
            continue;
        }

        anim_update(&c.sm, &c.eyes, now_ms);
        eyes_update(&c.eyes, now_ms, c.shapes);

        /* Dirty rects: union of each eye's previous and current bounding box. */
        rect_t dirty[2];
        int ndirty = 0;
        for (int i = 0; i < 2; i++) {
            const rect_t cur = shape_rect(&c.shapes[i]);
            const rect_t u = rect_align(rect_union(&c.prev[i], &cur));
            c.prev[i] = cur;
            if (!rect_is_empty(&u)) {
                dirty[ndirty++] = u;
            }
        }
        if (ndirty == 2 && rect_overlaps(&dirty[0], &dirty[1])) {
            dirty[0] = rect_union(&dirty[0], &dirty[1]);
            ndirty = 1;
        }

        /* Pacing: every TE edge when ACTIVE; every Nth edge, light-sleeping in between, when DROWSY. */
        if (state == POWER_DROWSY) {
            power_allow_light_sleep(true);
            display_delay_until_frame(DROWSY_PERIOD);
            power_allow_light_sleep(false);
        }
        if (!display_wait_vsync(VSYNC_TIMEOUT_MS)) {
            vsync_miss++;
        }
        const int64_t t_frame = esp_timer_get_time();

        for (int i = 0; i < ndirty; i++) {
            push_rect(&dirty[i], c.shapes);
        }
        display_wait_idle();
        brightness_step(state == POWER_ACTIVE ? 3 : 2);

        const uint32_t frame_us = (uint32_t)(esp_timer_get_time() - t_frame);
        frames++;
        rects_sum += (uint32_t)ndirty;
        frame_us_sum += frame_us;
        if (frame_us > frame_us_max) frame_us_max = frame_us;

        const int64_t now_us = esp_timer_get_time();
        if (now_us - stats_t0 >= STATS_PERIOD_US && frames) {
            const uint32_t bytes = display_take_bytes();
            const uint32_t elapsed_ms = (uint32_t)((now_us - stats_t0) / 1000);
            pmic_battery_t b;
            power_battery(&b);
            ESP_LOGI(TAG, "%s %s: %" PRIu32 " fps | frame %" PRIu32 " us avg, %" PRIu32 " us max | %" PRIu32 " B/frame, %" PRIu32 " rect(s) | pace %s%s | bri %d%% | batt %u mV %d%%%s%s",
                     power_state_name(state), anim_name(c.sm.id),
                     (frames * 1000u + elapsed_ms / 2) / elapsed_ms,
                     (uint32_t)(frame_us_sum / frames), frame_us_max,
                     bytes / frames, (rects_sum + frames / 2) / frames,
                     display_te_active() ? "TE" : "timer",
                     vsync_miss ? " (missed vsync)" : "",
                     s_bri_cur,
                     b.mv, b.percent, b.charging ? " chg" : "", b.vbus ? " usb" : "");
            stats_t0 = now_us;
            frames = 0;
            vsync_miss = 0;
            frame_us_sum = 0;
            frame_us_max = 0;
            rects_sum = 0;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "companion eyes, ESP-IDF %s, reset reason %d, wake cause %d",
             esp_get_idf_version(), (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause());

    settings_init();
    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_ERROR_CHECK(display_init());

    s_tap_q = xQueueCreate(4, sizeof(tap_event_t));
    assert(s_tap_q);
    ESP_ERROR_CHECK(touch_init(s_tap_q));
    ESP_ERROR_CHECK(power_init());

    ESP_LOGI(TAG, "free internal %u B, free PSRAM %u B (renderer uses none)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    const BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 8192, NULL, configMAX_PRIORITIES - 3, NULL, 1);
    assert(ok == pdPASS);
}
