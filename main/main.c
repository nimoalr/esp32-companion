/*
 * Companion eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.75.
 *
 *   core 0: app_main (init), touch task (CST9217 -> tap queue)
 *   core 1: render task (power state machine -> animation -> dirty rects ->
 *           band rasteriser -> QSPI DMA)
 */
#include <inttypes.h>
#include <stdio.h>
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
#include "gfx.h"
#include "ui.h"
#include "audio.h"

static const char *TAG = "eyes";

#define VSYNC_TIMEOUT_MS   40
#define STATS_PERIOD_US    1000000
#define DROWSY_PERIOD      (60 / CONFIG_EYES_DROWSY_FPS)
#define UI_TIMEOUT_MS      (CONFIG_EYES_UI_TIMEOUT_S * 1000u)

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

typedef enum { MODE_EYES, MODE_UI } mode_t;

typedef struct {
    eyes_t eyes;
    anim_sm_t sm;
    raster_shape_t shapes[2];
    rect_t prev[2];
    anim_id_t saved_anim;      /* expression to restore after DROWSY */
    mode_t mode;
    ui_t ui;
} render_ctx_t;

/* Paint one UI rect through the band path: black, then the screen's elements clipped to the band. */
static void push_rect_ui(const rect_t *r, const ui_t *ui)
{
    const int w = r->x1 - r->x0;
    for (int y = r->y0; y < r->y1; y += DISPLAY_BAND_ROWS) {
        int rows = r->y1 - y;
        if (rows > DISPLAY_BAND_ROWS) rows = DISPLAY_BAND_ROWS;
        uint16_t *band = display_acquire_band();
        memset(band, 0, (size_t)w * rows * 2);
        const gfx_band_t gb = { .dst = band, .x0 = r->x0, .y0 = y, .w = w, .rows = rows };
        ui_paint(ui, &gb);
        display_push(r->x0, y, w, rows, band);
    }
}

static void enter_ui(render_ctx_t *c, bool first_boot, uint32_t now_ms)
{
    c->mode = MODE_UI;
    ui_init(&c->ui, &g_settings, first_boot, now_ms);
    ESP_LOGI(TAG, "UI: %s%s", ui_screen_name(c->ui.screen), first_boot ? " (first boot)" : "");
}

static void leave_ui(render_ctx_t *c, uint32_t now_ms)
{
    c->mode = MODE_EYES;
    display_fill_black();
    c->prev[0] = c->prev[1] = rect_empty();
    anim_set(&c->sm, &c->eyes, ANIM_NEUTRAL, now_ms);
    ESP_LOGI(TAG, "UI closed");
}

/* Keep the microphones running exactly while the DANCE expression is showing. */
static void sync_audio(const render_ctx_t *c)
{
    const bool want = c->mode == MODE_EYES && c->sm.id == ANIM_DANCE;
    if (want && !audio_running()) {
        if (audio_start() != ESP_OK) {
            ESP_LOGW(TAG, "dance mode without microphones");
        }
    } else if (!want && audio_running()) {
        audio_stop();
    }
}

static void run_ui_actions(render_ctx_t *c, uint32_t now_ms)
{
    ui_action_t a;
    while ((a = ui_take_action(&c->ui)) != UI_ACT_NONE) {
        switch (a) {
        case UI_ACT_DANCE:
            leave_ui(c, now_ms);
            anim_set(&c->sm, &c->eyes, ANIM_DANCE, now_ms);
            break;
        case UI_ACT_SAVE:
            settings_save();
            break;
        case UI_ACT_BRIGHTNESS:
            brightness_set_now(g_settings.brightness_active);
            break;
        case UI_ACT_EXIT:
            leave_ui(c, now_ms);
            break;
        default:
            break;
        }
    }
}

static ui_input_t map_touch(touch_event_type_t t)
{
    switch (t) {
    case TOUCH_LONG_PRESS:  return UI_IN_LONG;
    case TOUCH_SWIPE_UP:    return UI_IN_UP;
    case TOUCH_SWIPE_DOWN:  return UI_IN_DOWN;
    case TOUCH_SWIPE_LEFT:  return UI_IN_LEFT;
    case TOUCH_SWIPE_RIGHT: return UI_IN_RIGHT;
    default:                return UI_IN_TAP;
    }
}

static void drain_taps(void)
{
    touch_event_t ev;
    while (xQueueReceive(s_tap_q, &ev, 0) == pdTRUE) {
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
    if (audio_running()) audio_stop();
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
    c.mode = MODE_EYES;
    if (!g_settings.cal.valid) {
        enter_ui(&c, true, now_ms);
    }

    power_state_t state = power_state();

    /* stats */
    int64_t stats_t0 = esp_timer_get_time();
    uint32_t frames = 0, vsync_miss = 0;
    uint64_t frame_us_sum = 0;
    uint32_t frame_us_max = 0;
    uint32_t rects_sum = 0;

    for (;;) {
        now_ms = ms_now();

        /* Gestures. Eyes: tap = next expression, swipes = next/previous, hold = setup UI. */
        touch_event_t ev;
        while (xQueueReceive(s_tap_q, &ev, 0) == pdTRUE) {
            if (c.mode == MODE_UI) {
                ui_input(&c.ui, map_touch(ev.type), now_ms);
            } else if (state == POWER_ACTIVE) {
                switch (ev.type) {
                case TOUCH_TAP:
                case TOUCH_SWIPE_LEFT:  anim_next(&c.sm, &c.eyes, now_ms); break;
                case TOUCH_SWIPE_RIGHT: anim_prev(&c.sm, &c.eyes, now_ms); break;
                case TOUCH_LONG_PRESS:  enter_ui(&c, false, now_ms); break;
                default: break;
                }
            }
        }
        if (c.mode == MODE_UI) {
            run_ui_actions(&c, now_ms);
        }
        sync_audio(&c);
        audio_features_t af = { 0 };
        if (audio_running()) {
            audio_get_features(&af);
            anim_set_audio(&c.sm, &af);
        }

        /* Power state machine. The UI counts as activity while it is being used. */
        uint32_t activity_ms = touch_last_activity_ms();
        if (c.mode == MODE_UI && (int32_t)(c.ui.last_input_ms - activity_ms) > 0) {
            activity_ms = c.ui.last_input_ms;
        }
        if (af.active && af.last_beat_ms && (int32_t)(af.last_beat_ms - activity_ms) > 0) {
            activity_ms = af.last_beat_ms;      /* music playing counts as company */
        }
        const power_state_t next = power_update(now_ms, activity_ms);
        if (c.mode == MODE_UI) {
            if (next != POWER_ACTIVE || now_ms - c.ui.last_input_ms >= UI_TIMEOUT_MS) {
                if (c.ui.first_boot && next == POWER_ACTIVE) {
                    c.ui.last_input_ms = now_ms;     /* the wizard waits as long as it takes */
                } else {
                    leave_ui(&c, now_ms);
                }
            }
        }
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

        rect_t dirty[UI_MAX_DIRTY];
        int ndirty = 0;
        if (c.mode == MODE_UI) {
            ui_sensors_t sens = { 0 };
            pmic_battery_t b;
            power_battery(&b);
            sens.have_accel = power_last_accel(sens.accel, &sens.accel_ms);
            sens.batt_mv = b.mv;
            sens.batt_pct = b.present ? b.percent : -1;
            sens.charging = b.charging;
            sens.usb = b.vbus;
            ui_rect_t ur[UI_MAX_DIRTY];
            const int n = ui_update(&c.ui, now_ms, &sens, ur);
            for (int i = 0; i < n; i++) {
                const rect_t r = rect_align((rect_t){ ur[i].x0, ur[i].y0, ur[i].x1, ur[i].y1 });
                if (!rect_is_empty(&r)) dirty[ndirty++] = r;
            }
        } else {
            anim_update(&c.sm, &c.eyes, now_ms);
            eyes_update(&c.eyes, now_ms, c.shapes);

            /* Dirty rects: union of each eye's previous and current bounding box. */
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
            if (c.mode == MODE_UI) push_rect_ui(&dirty[i], &c.ui);
            else push_rect(&dirty[i], c.shapes);
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
            char audio_s[48] = "";
            if (af.active) {
                snprintf(audio_s, sizeof audio_s, " | audio %" PRIu32 " us/frame, %d bpm", af.cpu_us, (int)af.bpm);
            }
            ESP_LOGI(TAG, "%s %s: %" PRIu32 " fps | frame %" PRIu32 " us avg, %" PRIu32 " us max | %" PRIu32 " B/frame, %" PRIu32 " rect(s) | pace %s%s | bri %d%% | batt %u mV %d%%%s%s%s",
                     power_state_name(state), c.mode == MODE_UI ? ui_screen_name(c.ui.screen) : anim_name(c.sm.id),
                     (frames * 1000u + elapsed_ms / 2) / elapsed_ms,
                     (uint32_t)(frame_us_sum / frames), frame_us_max,
                     bytes / frames, (rects_sum + frames / 2) / frames,
                     display_te_active() ? "TE" : "timer",
                     vsync_miss ? " (missed vsync)" : "",
                     s_bri_cur,
                     b.mv, b.percent, b.charging ? " chg" : "", b.vbus ? " usb" : "", audio_s);
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

    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_ERROR_CHECK(display_init());

    s_tap_q = xQueueCreate(8, sizeof(touch_event_t));
    assert(s_tap_q);
    ESP_ERROR_CHECK(touch_init(s_tap_q));
    ESP_ERROR_CHECK(power_init());

    ESP_LOGI(TAG, "free internal %u B, free PSRAM %u B (renderer uses none)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    const BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 8192, NULL, configMAX_PRIORITIES - 3, NULL, 1);
    assert(ok == pdPASS);
}
