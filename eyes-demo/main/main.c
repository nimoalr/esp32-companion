/*
 * Eyes render demo, stage 2: static eyes with the idle blink/saccade layer.
 *
 *   core 0: app_main (init)
 *   core 1: render task (animation -> dirty rects -> band rasteriser -> QSPI DMA)
 */
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "board.h"
#include "display.h"
#include "raster.h"
#include "eyes.h"

static const char *TAG = "eyes";

#define VSYNC_TIMEOUT_MS   40
#define STATS_PERIOD_US    1000000

typedef struct {
    int x0, y0, x1, y1;     /* [x0, x1) x [y0, y1) in pixels; empty when x0 >= x1 */
} rect_t;

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

static void render_task(void *arg)
{
    eyes_t eyes;
    raster_shape_t shapes[2];
    rect_t prev[2] = { rect_empty(), rect_empty() };

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    eyes_init(&eyes, now_ms);

    display_fill_black();

    /* stats */
    int64_t stats_t0 = esp_timer_get_time();
    uint32_t frames = 0, vsync_miss = 0;
    uint64_t frame_us_sum = 0;
    uint32_t frame_us_max = 0;
    uint32_t rects_sum = 0;

    for (;;) {

        now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        eyes_update(&eyes, now_ms, shapes);

        /* Dirty rects: union of each eye's previous and current bounding box. */
        rect_t dirty[2];
        int ndirty = 0;
        for (int i = 0; i < 2; i++) {
            const rect_t cur = shape_rect(&shapes[i]);
            const rect_t u = rect_align(rect_union(&prev[i], &cur));
            prev[i] = cur;
            if (!rect_is_empty(&u)) {
                dirty[ndirty++] = u;
            }
        }
        if (ndirty == 2 && rect_overlaps(&dirty[0], &dirty[1])) {
            dirty[0] = rect_union(&dirty[0], &dirty[1]);
            ndirty = 1;
        }

        /* Start the panel write on the next V-blank so the write outruns the scan. */
        if (!display_wait_vsync(VSYNC_TIMEOUT_MS)) {
            vsync_miss++;
        }
        const int64_t t_frame = esp_timer_get_time();

        for (int i = 0; i < ndirty; i++) {
            push_rect(&dirty[i], shapes);
        }
        display_wait_idle();

        const uint32_t frame_us = (uint32_t)(esp_timer_get_time() - t_frame);
        frames++;
        rects_sum += (uint32_t)ndirty;
        frame_us_sum += frame_us;
        if (frame_us > frame_us_max) frame_us_max = frame_us;

        const int64_t now_us = esp_timer_get_time();
        if (now_us - stats_t0 >= STATS_PERIOD_US) {
            const uint32_t bytes = display_take_bytes();
            const uint32_t elapsed_ms = (uint32_t)((now_us - stats_t0) / 1000);
            ESP_LOGI(TAG, "%" PRIu32 " fps | frame %" PRIu32 " us avg, %" PRIu32 " us max (raster+DMA) | %" PRIu32 " B/frame in %" PRIu32 " rect(s) | pace %s%s",
                     (frames * 1000u + elapsed_ms / 2) / elapsed_ms,
                     (uint32_t)(frame_us_sum / frames), frame_us_max,
                     bytes / frames, (rects_sum + frames / 2) / frames,
                     display_te_active() ? "TE" : "timer",
                     vsync_miss ? " (missed vsync)" : "");
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
    ESP_LOGI(TAG, "eyes render demo, ESP-IDF %s", esp_get_idf_version());

    ESP_ERROR_CHECK(display_init());

    ESP_LOGI(TAG, "free internal %u B, free PSRAM %u B (renderer uses none)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    const BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 8192, NULL, configMAX_PRIORITIES - 3, NULL, 1);
    assert(ok == pdPASS);
}
