/*
 * Stage 1: display bring-up. Paint the panel black, then push one solid
 * orange rectangle through the band buffers as a partial-window write.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "board.h"
#include "display.h"

static const char *TAG = "eyes";

/* RGB565 0xFF8C00 (orange), high byte first for the panel. */
#define ORANGE_BE 0x64FC

void app_main(void)
{
    ESP_LOGI(TAG, "eyes render demo, stage 1: display bring-up");
    ESP_ERROR_CHECK(display_init());

    display_fill_black();

    /* One 110 x 140 px rect at the left eye position, pushed in 32-row bands. */
    const int x = 78, y0 = 163, w = 110, h = 140;
    const int64_t t0 = esp_timer_get_time();
    for (int y = y0; y < y0 + h; y += DISPLAY_BAND_ROWS) {
        int rows = y0 + h - y;
        if (rows > DISPLAY_BAND_ROWS) rows = DISPLAY_BAND_ROWS;
        uint16_t *band = display_acquire_band();
        for (int i = 0; i < w * rows; i++) band[i] = ORANGE_BE;
        display_push(x, y, w, rows, band);
    }
    display_wait_idle();
    ESP_LOGI(TAG, "partial window %dx%d pushed in %lld us, %lu B, pace %s",
             w, h, (long long)(esp_timer_get_time() - t0), (unsigned long)display_take_bytes(),
             display_te_active() ? "TE" : "timer");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
