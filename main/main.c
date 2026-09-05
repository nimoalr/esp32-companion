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
#include "gfx.h"
#include "audio.h"
#include "accessories.h"
#include "behavior.h"

static const char *TAG = "eyes";

#define VSYNC_TIMEOUT_MS   40
#define STATS_PERIOD_US    1000000
#define DROWSY_PERIOD      (60 / CONFIG_EYES_DROWSY_FPS)
#define UI_TIMEOUT_MS      (CONFIG_EYES_UI_TIMEOUT_S * 1000u)
#define RECT_GRID          16
#define FB_BYTES           (DISPLAY_FRAME_BYTES * 3)     /* dirty rects may overlap; three screens of room */

typedef struct {
    int x0, y0, x1, y1;     /* [x0, x1) x [y0, y1) in pixels; empty when x0 >= x1 */
} rect_t;

static QueueHandle_t s_tap_q;
static void push_drain(void);

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
    /* 16 px grid: w * h * 2 is then a multiple of 64 (except at the clipped screen edge),
     * which lets the SPI driver DMA straight from PSRAM without a bounce copy. */
    r.x0 &= ~(RECT_GRID - 1);
    r.y0 &= ~(RECT_GRID - 1);
    r.x1 = (r.x1 + RECT_GRID - 1) & ~(RECT_GRID - 1);
    r.y1 = (r.y1 + RECT_GRID - 1) & ~(RECT_GRID - 1);
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
    anim_id_t user_anim;       /* what the user picked by tapping; behaviour may override it */
    mode_t mode;
    ui_t ui;
    behavior_t beh;
    accessories_t acc;
    uint32_t tap_count;
    bool mic_ok;
} render_ctx_t;

/*
 * Second raster core. The render task takes the top half of every band piece,
 * the worker on core 0 the bottom half; the eye rasteriser is reentrant and
 * the band buffers are in uncached internal SRAM, so no further sync is needed.
 */
typedef struct {
    uint16_t *dst;
    int x0, y, w, rows;
    const raster_shape_t *shapes;
} raster_job_t;

static raster_job_t s_wjob;
static TaskHandle_t s_worker, s_render;
static uint32_t s_frame_no;

static void raster_worker(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        raster_band(s_wjob.dst, s_wjob.x0, s_wjob.y, s_wjob.w, s_wjob.rows, s_wjob.shapes, 2);
        xTaskNotifyGive(s_render);
    }
}

static void raster_split(uint16_t *band, int x0, int y, int w, int rows, const raster_shape_t *shapes)
{
    if (!s_worker || rows < 4) {
        raster_band(band, x0, y, w, rows, shapes, 2);
        return;
    }
    const int top = rows / 2;
    s_wjob = (raster_job_t){ band + (size_t)top * w, x0, y + top, w, rows - top, shapes };
    xTaskNotifyGive(s_worker);
    raster_band(band, x0, y, w, top, shapes, 2);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

/* Eyes + accessories, or a UI screen, into one band piece. */
static void paint_piece(uint16_t *band, int x0, int y, int w, int rows, const render_ctx_t *c, uint32_t now_ms)
{
    if (c->mode == MODE_UI) {
        memset(band, 0, (size_t)w * rows * 2);
        const gfx_band_t gb = { .dst = band, .x0 = x0, .y0 = y, .w = w, .rows = rows };
        ui_paint(&c->ui, &gb);
    } else {
        raster_split(band, x0, y, w, rows, c->shapes);
        if (acc_any(&c->acc)) {
            const gfx_band_t gb = { .dst = band, .x0 = x0, .y0 = y, .w = w, .rows = rows };
            acc_paint(&c->acc, &gb, now_ms);
        }
    }
}


/*
 * Frame path. The panel latches its whole frame memory at the TE edge, so a
 * write only tears when it straddles an edge. Every frame is therefore
 * rastered completely into a PSRAM frame buffer first (both cores), and pushed
 * from there right after the next TE edge at bus speed. Two buffers alternate
 * so the raster of frame N+1 overlaps the transfer of frame N.
 */
typedef struct {
    uint16_t *base;
    size_t bytes;
    bool internal;          /* DMA straight from here; otherwise copy through the bounce buffers */
    uint32_t seq;           /* sequence number of the last push from this buffer */
    SemaphoreHandle_t free; /* given by the push task once it has issued everything from here */
} framebuf_t;

typedef struct {
    rect_t r;
    const uint16_t *pixels;
} frame_piece_t;

#define FB_SLOTS   3        /* two in PSRAM (pipelined), one internal (frames that fit skip the copy) */
#define FB_INTERNAL 2
static framebuf_t s_fb[FB_SLOTS];
static int s_fb_psram_cur;

#define MAX_PIECES      (UI_MAX_DIRTY + ACC_MAX_DIRTY + 3)

typedef struct {
    frame_piece_t pieces[MAX_PIECES];
    int n;
    int fb;                 /* frame buffer the pieces live in */
} push_job_t;

static QueueHandle_t s_push_q;            /* one frame ahead at most */
static volatile uint32_t s_push_us_sum, s_push_frames, s_vsync_miss;

static void fb_init(void)
{
    for (int i = 0; i < 2; i++) {
        s_fb[i].base = heap_caps_aligned_alloc(64, FB_BYTES, MALLOC_CAP_SPIRAM);
        s_fb[i].bytes = FB_BYTES;
        if (!s_fb[i].base) {
            ESP_LOGE(TAG, "no PSRAM for frame buffer %d (%u B)", i, (unsigned)FB_BYTES);
            abort();
        }
    }
    /* internal slot: as much as the heap can spare while leaving room for audio and the rest */
    size_t want = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    want = want > 56 * 1024 ? want - 56 * 1024 : 0;
    if (want > DISPLAY_DIRECT_MAX) want = DISPLAY_DIRECT_MAX;
    want &= ~(size_t)63;
    framebuf_t *ib = &s_fb[FB_INTERNAL];
    ib->base = want >= 32 * 1024 ? heap_caps_aligned_alloc(64, want, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) : NULL;
    ib->bytes = ib->base ? want : 0;
    ib->internal = true;
    for (int i = 0; i < FB_SLOTS; i++) {
        s_fb[i].free = xSemaphoreCreateBinary();
        xSemaphoreGive(s_fb[i].free);
    }
    ESP_LOGI(TAG, "frame buffers: 2 x %u KB PSRAM, %u KB internal (frames up to that size go straight to DMA)",
             (unsigned)(FB_BYTES / 1024), (unsigned)(ib->bytes / 1024));
}

/* Raster every dirty rect into the next frame buffer; fills `job` (pieces and buffer index). */
static void render_frame(const rect_t *dirty, int ndirty, const render_ctx_t *c, uint32_t now_ms, push_job_t *job, uint32_t *raster_us)
{
    /* Where does this frame go? Internal if it fits (no copy on the way out), else the next PSRAM buffer. */
    size_t need = 0;
    for (int i = 0; i < ndirty; i++) {
        need += ((size_t)(dirty[i].x1 - dirty[i].x0) * (size_t)(dirty[i].y1 - dirty[i].y0) * 2u + 63) & ~(size_t)63;
    }
    int slot;
    if (need <= s_fb[FB_INTERNAL].bytes) {
        slot = FB_INTERNAL;
    } else {
        slot = s_fb_psram_cur;
        s_fb_psram_cur ^= 1;
    }
    framebuf_t *fb = &s_fb[slot];
    xSemaphoreTake(fb->free, portMAX_DELAY);    /* the push task has let go of this buffer... */
    display_wait_done(fb->seq);                 /* ...and its last transfer is off the bus */
    const int64_t t0 = esp_timer_get_time();
    frame_piece_t *pieces = job->pieces;
    job->fb = slot;
    size_t off = 0;
    int n = 0;
    for (int i = 0; i < ndirty; i++) {
        const rect_t *r = &dirty[i];
        const int w = r->x1 - r->x0, h = r->y1 - r->y0;
        const size_t bytes = (size_t)w * (size_t)h * 2u;
        if (off + bytes > fb->bytes) {
            ESP_LOGW(TAG, "frame buffer full: dropping rect %d", i);
            continue;
        }
        uint16_t *block = fb->base + off / 2;
        for (int y = r->y0; y < r->y1; y += DISPLAY_BAND_ROWS) {
            const int rows = (r->y1 - y) < DISPLAY_BAND_ROWS ? (r->y1 - y) : DISPLAY_BAND_ROWS;
            paint_piece(block + (size_t)(y - r->y0) * w, r->x0, y, w, rows, c, now_ms);
        }
        pieces[n++] = (frame_piece_t){ *r, block };
        off += (bytes + 63) & ~(size_t)63;
    }
    job->n = n;
    *raster_us = (uint32_t)(esp_timer_get_time() - t0);
}

/*
 * Push task. Frames go out right after the TE edge, as fast as the bus allows
 * (80 MHz QSPI: 23 us per full row, faster than the panel's own scan, so a
 * write that starts at the edge is never overtaken), and the push runs here so
 * the next frame's raster overlaps it. Frames in the internal buffer are DMA'd
 * directly, one transfer per rect. Frames in PSRAM cannot feed the SPI DMA
 * without underruns, so they go through the bounce buffers, each band copied
 * by the GDMA memcpy engine while the previous band is on the bus.
 */
static void push_psram_rect(framebuf_t *fb, const frame_piece_t *p)
{
    const rect_t *r = &p->r;
    const int w = r->x1 - r->x0;
    const int nb = (r->y1 - r->y0 + DISPLAY_BAND_ROWS - 1) / DISPLAY_BAND_ROWS;
    uint16_t *band = NULL, *next = NULL;
    bool dma = false;
    for (int k = 0; k < nb; k++) {
        const int y = r->y0 + k * DISPLAY_BAND_ROWS;
        const int rows = (r->y1 - y) < DISPLAY_BAND_ROWS ? (r->y1 - y) : DISPLAY_BAND_ROWS;
        const uint16_t *src = p->pixels + (size_t)(y - r->y0) * w;
        const size_t bytes = (size_t)w * rows * 2u;
        if (k == 0) {
            band = display_acquire_band();
            dma = display_copy_start(band, src, bytes);
            if (!dma) memcpy(band, src, bytes);
        }
        if (dma) display_copy_wait();
        /* start the next band's copy before this one goes on the bus */
        if (k + 1 < nb) {
            const int y2 = y + DISPLAY_BAND_ROWS;
            const int rows2 = (r->y1 - y2) < DISPLAY_BAND_ROWS ? (r->y1 - y2) : DISPLAY_BAND_ROWS;
            const uint16_t *src2 = p->pixels + (size_t)(y2 - r->y0) * w;
            const size_t bytes2 = (size_t)w * rows2 * 2u;
            next = display_acquire_band();
            dma = display_copy_start(next, src2, bytes2);
            if (!dma) memcpy(next, src2, bytes2);
        }
        fb->seq = display_push(r->x0, y, w, rows, band);
        band = next;
    }
}

static void push_task(void *arg)
{
    push_job_t job;
    for (;;) {
        xQueueReceive(s_push_q, &job, portMAX_DELAY);
        if (!display_wait_vsync(VSYNC_TIMEOUT_MS)) s_vsync_miss++;
        const int64_t t0 = esp_timer_get_time();
        framebuf_t *fb = &s_fb[job.fb];
        for (int i = 0; i < job.n; i++) {
            const rect_t *r = &job.pieces[i].r;
            if (fb->internal) {
                fb->seq = display_push_direct(r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0, job.pieces[i].pixels);
            } else {
                push_psram_rect(fb, &job.pieces[i]);
            }
        }
        s_push_us_sum += (uint32_t)(esp_timer_get_time() - t0);
        s_push_frames++;
        xSemaphoreGive(fb->free);
    }
}

/* Wait until every queued frame has been pushed and the bus is idle (before touching the panel directly). */
static void push_drain(void)
{
    for (int i = 0; i < FB_SLOTS; i++) xSemaphoreTake(s_fb[i].free, portMAX_DELAY);
    display_wait_idle();
    for (int i = 0; i < FB_SLOTS; i++) xSemaphoreGive(s_fb[i].free);
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
    push_drain();
    display_fill_black();
    c->prev[0] = c->prev[1] = rect_empty();
    acc_init(&c->acc, BOARD_LCD_H_RES / 2 - 95, BOARD_LCD_H_RES / 2 + 95, BOARD_LCD_V_RES / 2);
    c->user_anim = ANIM_NEUTRAL;
    anim_set(&c->sm, &c->eyes, ANIM_NEUTRAL, now_ms);
    ESP_LOGI(TAG, "UI closed");
}

/* Keep the microphones running exactly while the DANCE expression is showing. */
static void sync_audio(render_ctx_t *c, bool want)
{
    want = want && c->mode == MODE_EYES;
    if (want && !audio_running()) {
        if (audio_start() != ESP_OK) {
            ESP_LOGW(TAG, "microphones unavailable");
            c->mic_ok = false;
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
            c->user_anim = ANIM_DANCE;
            anim_set(&c->sm, &c->eyes, ANIM_DANCE, now_ms);
            break;
        case UI_ACT_SAVE:
            settings_save();
            break;
        case UI_ACT_BRIGHTNESS:
            brightness_set_now(g_settings.brightness_active);
            break;
        case UI_ACT_COLOR:
            eyes_set_base_color(&c->eyes, settings_eye_rgb());
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
    eye_pose_t closed = EYE_POSE_NEUTRAL;
    closed.sy = Q16(0.03);
    closed.sx = Q16(1.25);
    eyes_set_target(&c->eyes, 0, &closed, 1, now_ms);
    eyes_set_target(&c->eyes, 1, &closed, 1, now_ms);
    eyes_update(&c->eyes, now_ms + 2, c->shapes);   /* settle the closed pose */
}

/* SLEEP: panel off, light sleep until motion/touch (-> ACTIVE) or the deadline (-> DEEP). */
static void do_sleep(render_ctx_t *c)
{
    push_drain();
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
    acc_redraw(&c->acc);
    c->user_anim = ANIM_NEUTRAL;
    anim_set(&c->sm, &c->eyes, ANIM_NEUTRAL, ms_now());
    brightness_set_now(g_settings.brightness_active);
    drain_taps();
}

static void on_transition(render_ctx_t *c, power_state_t from, power_state_t to, uint32_t now_ms)
{
    switch (to) {
    case POWER_DROWSY:
        c->saved_anim = c->user_anim;
        c->user_anim = ANIM_SLEEPY;
        anim_set(&c->sm, &c->eyes, ANIM_SLEEPY, now_ms);
        brightness_target(g_settings.brightness_aod);
        break;
    case POWER_ACTIVE:
        if (from == POWER_DROWSY) {
            c->user_anim = c->saved_anim;
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
    s_render = xTaskGetCurrentTaskHandle();
    fb_init();
    s_push_q = xQueueCreate(1, sizeof(push_job_t));
    assert(s_push_q);
    const BaseType_t pok = xTaskCreatePinnedToCore(push_task, "push", 4096, NULL, configMAX_PRIORITIES - 2, NULL, 1);
    assert(pok == pdPASS);
    if (xTaskCreatePinnedToCore(raster_worker, "raster0", 8192, NULL, 8, &s_worker, 0) != pdPASS) {
        s_worker = NULL;
        ESP_LOGW(TAG, "no raster worker: single-core raster");
    }
    uint32_t now_ms = ms_now();
    eyes_init(&c.eyes, now_ms);
    eyes_set_base_color(&c.eyes, settings_eye_rgb());
    eyes_set_hotspot(&c.eyes, true);      /* shaded eyes, always: no measurable frame cost on this build */
    anim_init(&c.sm, &c.eyes, now_ms);
    c.prev[0] = c.prev[1] = rect_empty();
    c.saved_anim = ANIM_NEUTRAL;
    c.user_anim = ANIM_NEUTRAL;
    c.mic_ok = true;
    behavior_init(&c.beh, now_ms);
    acc_init(&c.acc, BOARD_LCD_H_RES / 2 - 95, BOARD_LCD_H_RES / 2 + 95, BOARD_LCD_V_RES / 2);

    display_fill_black();
    brightness_set_now(g_settings.brightness_active);
    c.mode = MODE_EYES;
    if (!g_settings.cal.valid) {
        enter_ui(&c, true, now_ms);
    }

    power_state_t state = power_state();

    /* stats */
    int64_t stats_t0 = esp_timer_get_time();
    uint32_t frames = 0, te_edges_prev = 0;
    uint64_t raster_us_sum = 0;
    uint32_t raster_us_max = 0;
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
                case TOUCH_SWIPE_LEFT:
                    c.tap_count++;
                    c.user_anim = (anim_id_t)((c.user_anim + 1) % ANIM_COUNT);
                    break;
                case TOUCH_SWIPE_RIGHT:
                    c.tap_count++;
                    c.user_anim = (anim_id_t)((c.user_anim + ANIM_COUNT - 1) % ANIM_COUNT);
                    break;
                case TOUCH_SWIPE_DOWN:  enter_ui(&c, false, now_ms); break;
                default: break;   /* a long press is attention (handled below), not a gesture */
                }
            }
        }
        if (c.mode == MODE_UI) {
            run_ui_actions(&c, now_ms);
        }

        /* Character: environment reactions decide what is shown and whether the mics run. */
        audio_features_t af = { 0 };
        if (audio_running()) {
            audio_get_features(&af);
        }
        behavior_out_t bo = { .override_anim = -1 };
        if (c.mode == MODE_EYES) {
            behavior_in_t bi = { .cal = &g_settings.cal, .audio = af, .mic_available = c.mic_ok,
                                 .user_interacting = (int32_t)(now_ms - touch_last_activity_ms()) < 3000, .tap_count = c.tap_count };
            bi.have_accel = power_last_accel(bi.accel, &bi.accel_ms);
            behavior_update(&c.beh, &bi, now_ms, &bo);
            const anim_id_t want = bo.override_anim >= 0 ? (anim_id_t)bo.override_anim : c.user_anim;
            if (want != c.sm.id && state == POWER_ACTIVE) {
                anim_set(&c.sm, &c.eyes, want, now_ms);
            }
            eyes_set_env(&c.eyes, 0, &bo.env[0]);
            eyes_set_env(&c.eyes, 1, &bo.env[1]);
            /* a finger resting on the screen: the eyes settle on it and stop wandering (Vector's focus) */
            uint16_t fx, fy;
            const bool finger = touch_pressed(&fx, &fy);
            eyes_set_attention(&c.eyes, finger, fx, fy);
            /* mood: a tired character is dimmer and paler, an energetic one glows */
            const float energy = behavior_energy(&c.beh);
            eyes_set_mood(&c.eyes, (int32_t)((0.85f + 0.15f * energy) * 65536.f), (int32_t)((0.90f + 0.10f * energy) * 65536.f));
            eyes_set_face_angle(&c.eyes, bo.face_angle_deg);
            acc_set_angle(&c.acc, bo.face_angle_deg);
            {
                pmic_battery_t b;
                power_battery(&b);
                acc_set_charge(&c.acc, b.vbus, b.present ? b.percent : 0, b.charging);
            }
            acc_set_knocked_out(&c.acc, bo.knocked_out, now_ms);
            acc_set_zz(&c.acc, bo.zz || c.sm.id == ANIM_SLEEPING, now_ms);
        }
        sync_audio(&c, (c.sm.id == ANIM_DANCE && bo.override_anim < 0) || bo.want_mic);
        if (audio_running()) {
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

        rect_t dirty[UI_MAX_DIRTY + ACC_MAX_DIRTY + 2];
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
            if (bo.knocked_out) {
                c.shapes[0].visible = c.shapes[1].visible = false;   /* X eyes are drawn as accessories */
            }

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
            acc_rect_t ar[ACC_MAX_DIRTY];
            const int na = acc_update(&c.acc, now_ms, ar);
            for (int i = 0; i < na; i++) {
                const rect_t r = rect_align((rect_t){ ar[i].x0, ar[i].y0, ar[i].x1, ar[i].y1 });
                if (!rect_is_empty(&r)) dirty[ndirty++] = r;
            }
        }

        /* Raster the whole frame (into PSRAM) while the push task streams the previous one. */
        static push_job_t job;
        uint32_t raster_us;
        render_frame(dirty, ndirty, &c, now_ms, &job, &raster_us);
        s_frame_no++;

        /* Pacing: the push task locks every frame to a TE edge; when DROWSY only every Nth, light-sleeping in between. */
        if (state == POWER_DROWSY) {
            power_allow_light_sleep(true);
            display_delay_until_frame(DROWSY_PERIOD);
            power_allow_light_sleep(false);
        }
        xQueueSend(s_push_q, &job, portMAX_DELAY);
        brightness_step(state == POWER_ACTIVE ? 3 : 2);

        frames++;
        rects_sum += (uint32_t)ndirty;
        raster_us_sum += raster_us;
        if (raster_us > raster_us_max) raster_us_max = raster_us;

        const int64_t now_us = esp_timer_get_time();
        if (now_us - stats_t0 >= STATS_PERIOD_US && frames) {
            const uint32_t bytes = display_take_bytes();
            const uint32_t elapsed_ms = (uint32_t)((now_us - stats_t0) / 1000);
            pmic_battery_t b;
            power_battery(&b);
            char audio_s[96] = "";
            if (af.active) {
                snprintf(audio_s, sizeof audio_s, " | audio %" PRIu32 " us, rms %.0f LSB, loud %.2f bass %.2f kick %.2f, beats %" PRIu32 " %d bpm reg %.2f",
                         af.cpu_us, af.raw_loud, af.loud, af.bass, af.kick, af.beat_count, (int)af.bpm, af.regularity);
            }
            ESP_LOGI(TAG, "%s %s [%s, energy %.2f]: %" PRIu32 " fps | raster %" PRIu32 " us avg, %" PRIu32 " us max | push %" PRIu32 " us | %" PRIu32 " B/frame, %" PRIu32 " rect(s) | pace %s%s (%" PRIu32 " TE/s) | bri %d%% | batt %u mV %d%%%s%s%s | stack %u B free",
                     power_state_name(state), c.mode == MODE_UI ? ui_screen_name(c.ui.screen) : anim_name(c.sm.id),
                     behavior_state_name(c.beh.state), behavior_energy(&c.beh),
                     (frames * 1000u + elapsed_ms / 2) / elapsed_ms,
                     (uint32_t)(raster_us_sum / frames), raster_us_max, s_push_frames ? s_push_us_sum / s_push_frames : 0,
                     bytes / frames, (rects_sum + frames / 2) / frames,
                     display_te_active() ? "TE" : "timer",
                     s_vsync_miss ? " (missed vsync)" : "", display_te_edges() - te_edges_prev,
                     s_bri_cur,
                     b.mv, b.percent, b.charging ? " chg" : "", b.vbus ? " usb" : "", audio_s,
                     (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
            stats_t0 = now_us;
            te_edges_prev = display_te_edges();
            frames = 0;
            raster_us_sum = 0;
            s_push_us_sum = 0;
            s_push_frames = 0;
            s_vsync_miss = 0;
            raster_us_max = 0;
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

    const BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 16384, NULL, configMAX_PRIORITIES - 3, NULL, 1);
    assert(ok == pdPASS);
}
