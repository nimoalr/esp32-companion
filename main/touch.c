#include "touch.h"

#include <stdlib.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "i2c_bus.h"
#include "board.h"

static const char *TAG = "touch";

#define TAP_MAX_MS          300
#define TAP_MAX_MOVE_PX     10
#define LONG_PRESS_MS       700
#define SWIPE_MIN_PX        40
#define SWIPE_MAX_MS        1000
#define EVENT_DEBOUNCE_MS   150     /* ignore a new gesture this soon after the previous one */
#define BOUNCE_MS           40      /* a new touch this soon after a release is contact bounce */
#define POLL_TOUCHED_MS     10
#define POLL_IDLE_MS        500

static esp_lcd_touch_handle_t s_tp;
static SemaphoreHandle_t s_int_sem;
static QueueHandle_t s_q;
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

static void emit(touch_event_type_t type, uint16_t x, uint16_t y, int dx, int dy)
{
    const touch_event_t ev = { .type = type, .x = x, .y = y, .dx = (int16_t)dx, .dy = (int16_t)dy };
    if (xQueueSend(s_q, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full");
    }
}

static void touch_task(void *arg)
{
    bool touched = false, moved = false, long_sent = false;
    uint16_t down_x = 0, down_y = 0, last_x = 0, last_y = 0;
    uint32_t down_ms = 0, release_ms = 0, last_event_ms = 0;

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
            long_sent = false;
            down_x = last_x = x;
            down_y = last_y = y;
            down_ms = t;
        } else if (pressed && touched) {
            last_x = x;
            last_y = y;
            if (abs((int)x - (int)down_x) >= TAP_MAX_MOVE_PX || abs((int)y - (int)down_y) >= TAP_MAX_MOVE_PX) {
                moved = true;
            }
            if (!moved && !long_sent && t - down_ms >= LONG_PRESS_MS && t - last_event_ms >= EVENT_DEBOUNCE_MS) {
                long_sent = true;
                last_event_ms = t;
                emit(TOUCH_LONG_PRESS, down_x, down_y, 0, 0);
            }
        } else if (!pressed && touched) {
            touched = false;
            release_ms = t;
            const uint32_t held = t - down_ms;
            const int dx = (int)last_x - (int)down_x, dy = (int)last_y - (int)down_y;
            if (long_sent || t - last_event_ms < EVENT_DEBOUNCE_MS) {
                continue;
            }
            if (!moved && held <= TAP_MAX_MS) {
                last_event_ms = t;
                emit(TOUCH_TAP, down_x, down_y, 0, 0);
            } else if (held <= SWIPE_MAX_MS && (abs(dx) >= SWIPE_MIN_PX || abs(dy) >= SWIPE_MIN_PX)) {
                last_event_ms = t;
                if (abs(dx) >= abs(dy)) {
                    emit(dx > 0 ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT, down_x, down_y, dx, dy);
                } else {
                    emit(dy > 0 ? TOUCH_SWIPE_DOWN : TOUCH_SWIPE_UP, down_x, down_y, dx, dy);
                }
            }
        }
    }
}

esp_err_t touch_init(QueueHandle_t queue)
{
    s_q = queue;
    s_int_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_int_sem, ESP_ERR_NO_MEM, TAG, "semaphore");

    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c bus");
    i2c_master_bus_handle_t bus = i2c_bus_get();

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = BOARD_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(bus, &io_cfg, &io), TAG, "touch io");

    /* Mirroring follows the Waveshare BSP's LVGL configuration for this panel. */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = {
            .swap_xy = 0,
            .mirror_x = CONFIG_EYES_TOUCH_MIRROR_X,
            .mirror_y = CONFIG_EYES_TOUCH_MIRROR_Y,
        },
        .interrupt_callback = touch_isr_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9217(io, &tp_cfg, &s_tp), TAG, "cst9217");

    const BaseType_t ok = xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 10, NULL, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "CST9217 on I2C%d (SDA %d, SCL %d, %d kHz), INT GPIO%d, RST GPIO%d, mirror x=%d y=%d",
             BOARD_I2C_PORT, BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ / 1000, BOARD_TOUCH_INT, BOARD_TOUCH_RST,
             CONFIG_EYES_TOUCH_MIRROR_X, CONFIG_EYES_TOUCH_MIRROR_Y);
    return ESP_OK;
}

uint32_t touch_last_activity_ms(void)
{
    return s_last_activity_ms;
}
