#include "touch.h"

#include <stdlib.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "i2c_bus.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "board.h"

static const char *TAG = "touch";

#define TAP_MAX_MS          300     /* down -> up within this is a tap */
#define TAP_MAX_MOVE_PX     10      /* more travel than this is a drag */
#define TAP_DEBOUNCE_MS     150     /* ignore a tap this soon after the previous one */
#define BOUNCE_MS           40      /* a new touch this soon after a release is contact bounce */
#define POLL_TOUCHED_MS     10      /* re-read cadence while a finger is down */
#define POLL_IDLE_MS        500     /* safety poll in case an interrupt edge is missed */

static esp_lcd_touch_handle_t s_tp;
static SemaphoreHandle_t s_int_sem;
static QueueHandle_t s_tap_q;
static volatile uint32_t s_last_activity_ms;

static void IRAM_ATTR touch_isr_cb(esp_lcd_touch_handle_t tp)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_int_sem, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void touch_task(void *arg)
{
    bool touched = false;
    bool moved = false;
    uint16_t down_x = 0, down_y = 0;
    uint32_t down_ms = 0, release_ms = 0, last_tap_ms = 0;

    for (;;) {
        xSemaphoreTake(s_int_sem, pdMS_TO_TICKS(touched ? POLL_TOUCHED_MS : POLL_IDLE_MS));

        bool pressed = false;
        uint16_t x = 0, y = 0;
        if (esp_lcd_touch_read_data(s_tp) == ESP_OK) {
            esp_lcd_touch_point_data_t pt = { 0 };
            uint8_t n = 0;
            if (esp_lcd_touch_get_data(s_tp, &pt, &n, 1) == ESP_OK && n > 0) {
                pressed = true;
                x = pt.x;
                y = pt.y;
            }
        }
        const uint32_t t = now_ms();
        if (pressed) {
            s_last_activity_ms = t;
        }

        if (pressed && !touched) {
            if (t - release_ms < BOUNCE_MS) {
                continue;
            }
            touched = true;
            moved = false;
            down_x = x;
            down_y = y;
            down_ms = t;
        } else if (pressed && touched) {
            if (abs((int)x - (int)down_x) >= TAP_MAX_MOVE_PX || abs((int)y - (int)down_y) >= TAP_MAX_MOVE_PX) {
                moved = true;
            }
        } else if (!pressed && touched) {
            touched = false;
            release_ms = t;
            const uint32_t held = t - down_ms;
            if (!moved && held <= TAP_MAX_MS && (t - last_tap_ms) >= TAP_DEBOUNCE_MS) {
                last_tap_ms = t;
                const tap_event_t ev = { .x = down_x, .y = down_y };
                if (xQueueSend(s_tap_q, &ev, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "tap queue full");
                }
            }
        }
    }
}

esp_err_t touch_init(QueueHandle_t tap_queue)
{
    s_tap_q = tap_queue;
    s_int_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_int_sem, ESP_ERR_NO_MEM, TAG, "semaphore");

    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c bus");
    i2c_master_bus_handle_t bus = i2c_bus_get();

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = BOARD_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(bus, &io_cfg, &io), TAG, "touch io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .interrupt_callback = touch_isr_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9217(io, &tp_cfg, &s_tp), TAG, "cst9217");

    const BaseType_t ok = xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 10, NULL, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "CST9217 on I2C%d (SDA %d, SCL %d, %d kHz), INT GPIO%d, RST GPIO%d",
             BOARD_I2C_PORT, BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ / 1000, BOARD_TOUCH_INT, BOARD_TOUCH_RST);
    return ESP_OK;
}

uint32_t touch_last_activity_ms(void)
{
    return s_last_activity_ms;
}
