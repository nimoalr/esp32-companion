#include "display.h"

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "display";

/*
 * Panel init sequence, verbatim from the Waveshare BSP
 * (waveshare/esp32_s3_touch_amoled_1_75 v3.0.1, lcd_init_cmds[]).
 * 0x35 0x00 turns the tearing-effect output on (V-blank pulses only).
 */
static const co5300_lcd_init_cmd_t s_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;

static uint16_t *s_band[2];
static SemaphoreHandle_t s_band_free;     /* counting, one token per idle band buffer */
static uint32_t s_band_seq;               /* which buffer the next acquire hands out */

static SemaphoreHandle_t s_vsync;         /* given by the TE edge (or the fallback timer) */
static volatile uint32_t s_te_edges;
static volatile int64_t s_te_last_us;
static int64_t s_last_frame_te_us;
static bool s_te_active;
static esp_timer_handle_t s_pace_timer;

static volatile uint32_t s_bytes_pushed;

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_band_free, &woken);
    return woken == pdTRUE;
}

static void IRAM_ATTR te_isr(void *arg)
{
    s_te_edges++;
    s_te_last_us = esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_vsync, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void pace_timer_cb(void *arg)
{
    xSemaphoreGive(s_vsync);
}

esp_err_t display_init(void)
{
    for (int i = 0; i < 2; i++) {
        s_band[i] = heap_caps_malloc(DISPLAY_BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_RETURN_ON_FALSE(s_band[i], ESP_ERR_NO_MEM, TAG, "no internal DMA memory for band %d", i);
    }
    s_band_free = xSemaphoreCreateCounting(2, 2);
    s_vsync = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_band_free && s_vsync, ESP_ERR_NO_MEM, TAG, "no memory for semaphores");

    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(BOARD_LCD_PCLK, BOARD_LCD_DATA0, BOARD_LCD_DATA1,
                                                              BOARD_LCD_DATA2, BOARD_LCD_DATA3, DISPLAY_BAND_BYTES);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(BOARD_LCD_CS, on_color_done, NULL);
    io_cfg.pclk_hz = CONFIG_EYES_LCD_PCLK_HZ;
    io_cfg.trans_queue_depth = 4;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_cfg, &s_io), TAG, "panel io");

    const co5300_vendor_config_t vendor = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, BOARD_LCD_X_GAP, BOARD_LCD_Y_GAP), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    /* TE input: rising edge marks the start of the panel's V-blank. */
    const gpio_config_t te_cfg = {
        .pin_bit_mask = BIT64(BOARD_LCD_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te_cfg), TAG, "te gpio");
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "gpio isr service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BOARD_LCD_TE, te_isr, NULL), TAG, "te isr");

    /* Decide the pacing source: wait up to 500 ms for TE to start pulsing. */
    int waited_ms = 0;
    while (s_te_edges < 2 && waited_ms < 500) {
        vTaskDelay(pdMS_TO_TICKS(20));
        waited_ms += 20;
    }
    s_te_active = s_te_edges >= 2;
    if (s_te_active) {
        ESP_LOGI(TAG, "TE detected on GPIO%d (%" PRIu32 " edges after %d ms): frames locked to TE", BOARD_LCD_TE, s_te_edges, waited_ms);
    } else {
        gpio_isr_handler_remove(BOARD_LCD_TE);
        const esp_timer_create_args_t targs = {
            .callback = pace_timer_cb,
            .name = "pace60",
            .dispatch_method = ESP_TIMER_TASK,
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_pace_timer), TAG, "pace timer");
        ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_pace_timer, 16667), TAG, "pace timer start");
        ESP_LOGW(TAG, "no TE edges seen on GPIO%d: pacing frames with a 60 Hz timer", BOARD_LCD_TE);
    }

    ESP_LOGI(TAG, "CO5300 %dx%d over QSPI at %u MHz, 2 x %u B band buffers in internal SRAM",
             DISPLAY_W, DISPLAY_H, (unsigned)(CONFIG_EYES_LCD_PCLK_HZ / 1000000), (unsigned)DISPLAY_BAND_BYTES);
    return ESP_OK;
}

uint16_t *display_acquire_band(void)
{
    xSemaphoreTake(s_band_free, portMAX_DELAY);
    return s_band[s_band_seq++ & 1];
}

void display_push(int x, int y, int w, int h, const uint16_t *band)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, band));
    s_bytes_pushed += (uint32_t)w * (uint32_t)h * 2u;
}

void display_wait_idle(void)
{
    /* Both tokens available means nothing is in flight. */
    xSemaphoreTake(s_band_free, portMAX_DELAY);
    xSemaphoreTake(s_band_free, portMAX_DELAY);
    xSemaphoreGive(s_band_free);
    xSemaphoreGive(s_band_free);
}

bool display_wait_vsync(uint32_t timeout_ms)
{
    /* Drop a stale edge so the frame always starts on a fresh V-blank. */
    xSemaphoreTake(s_vsync, 0);
    const bool ok = xSemaphoreTake(s_vsync, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    s_last_frame_te_us = s_te_last_us;
    return ok;
}

#define TE_PERIOD_US   16667
#define TE_LEAD_US     2500     /* wake this early so light-sleep exit latency and tick jitter are covered */

void display_delay_until_frame(uint32_t period)
{
    if (period < 1) {
        period = 1;
    }
    const int64_t now = esp_timer_get_time();
    const int64_t ref = (s_te_active && s_last_frame_te_us) ? s_last_frame_te_us : now;
    const int64_t target = ref + (int64_t)period * TE_PERIOD_US - TE_LEAD_US;
    if (target > now) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)((target - now) / 1000)));
    }
}

/* QSPI command framing used by the CO5300 driver: opcode 0x02, command in bits 15:8. */
static void panel_cmd(uint8_t cmd, const uint8_t *param, size_t len)
{
    const int lcd_cmd = (0x02 << 24) | ((int)cmd << 8);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, lcd_cmd, param, len));
}

void display_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    const uint8_t v = (uint8_t)((percent * 255u) / 100u);
    panel_cmd(0x51, &v, 1);
}

void display_sleep(bool sleep)
{
    if (sleep) {
        panel_cmd(0x28, NULL, 0);           /* Display Off */
        vTaskDelay(pdMS_TO_TICKS(20));
        panel_cmd(0x10, NULL, 0);           /* Sleep In */
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        panel_cmd(0x11, NULL, 0);           /* Sleep Out */
        vTaskDelay(pdMS_TO_TICKS(120));
        display_fill_black();               /* GRAM is not trusted across sleep */
        panel_cmd(0x29, NULL, 0);           /* Display On */
    }
}

bool display_te_active(void)
{
    return s_te_active;
}

void display_fill_black(void)
{
    for (int y = 0; y < DISPLAY_H; y += DISPLAY_BAND_ROWS) {
        int rows = DISPLAY_H - y;
        if (rows > DISPLAY_BAND_ROWS) {
            rows = DISPLAY_BAND_ROWS;
        }
        uint16_t *b = display_acquire_band();
        memset(b, 0, (size_t)DISPLAY_W * rows * 2);
        display_push(0, y, DISPLAY_W, rows, b);
    }
    display_wait_idle();
}

uint32_t display_take_bytes(void)
{
    uint32_t v = s_bytes_pushed;
    s_bytes_pushed = 0;
    return v;
}

uint32_t display_pclk_hz(void)
{
    return CONFIG_EYES_LCD_PCLK_HZ;
}
