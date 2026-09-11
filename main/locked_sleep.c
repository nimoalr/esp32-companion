#include "locked_sleep.h"
#include "lock_policy.h"
#include "lock_screen.h"
#include "board.h"
#include "display.h"
#include "settings.h"
#include "power.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "locked_sleep";

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Soft reset does not reset the external codecs. These are the shutdown
 * sequences from the pinned esp_codec_dev es7210_stop / es8311_suspend.
 * No I2S clocks, codec opening, microphone task or audio buffers are needed. */
static esp_err_t quiet_codec(uint8_t address, const uint8_t (*regs)[2], size_t n)
{
    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_bus_add_device(address, &dev);
    if (err != ESP_OK) return err;
    esp_err_t first = ESP_OK;
    for (size_t i = 0; i < n; ++i) {
        err = i2c_master_transmit(dev, regs[i], 2, 100);
        if (first == ESP_OK) first = err;
    }
    i2c_master_bus_rm_device(dev);
    return first;
}

static void quiet_hardware(void)
{
    /* Hold the touch controller in reset and the amplifier in shutdown. */
    const gpio_config_t outputs = {
        .pin_bit_mask = BIT64(BOARD_PA_EN) | BIT64(BOARD_TOUCH_RST),
        .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(BOARD_PA_EN, 0);
    gpio_set_level(BOARD_TOUCH_RST, 0);
    ESP_ERROR_CHECK(gpio_config(&outputs));
    static const uint8_t adc[][2] = {
        {0x47,0xff},{0x48,0xff},{0x49,0xff},{0x4a,0xff},
        {0x4b,0xff},{0x4c,0xff},{0x40,0xc0},{0x01,0x7f},{0x06,0x07},
    };
    static const uint8_t dac[][2] = {
        {0x32,0x00},{0x17,0x00},{0x0e,0xff},{0x12,0x02},{0x14,0x00},
        {0x0d,0xfa},{0x15,0x00},{0x02,0x10},{0x00,0x00},{0x00,0x1f},
        {0x01,0x30},{0x01,0x00},{0x45,0x00},{0x0d,0xfc},{0x02,0x00},
    };
    esp_err_t a = quiet_codec(BOARD_ES7210_ADDR_7BIT, adc, sizeof(adc)/sizeof(adc[0]));
    esp_err_t d = quiet_codec(0x18, dac, sizeof(dac)/sizeof(dac[0]));
    if (a != ESP_OK || d != ESP_OK)
        ESP_LOGW(TAG, "codec shutdown ADC=%s DAC=%s; no audio tasks or clocks will start",
                 esp_err_to_name(a), esp_err_to_name(d));
}

void locked_sleep_run(void)
{
    quiet_hardware();
    const gpio_config_t boot = {
        .pin_bit_mask = BIT64(BOARD_BOOT_BUTTON), .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot));
    ESP_ERROR_CHECK(power_init());
    ESP_ERROR_CHECK(display_init());
    display_set_brightness(0);
    display_stop_pacing();
    for (int y = 0; y < DISPLAY_H; y += DISPLAY_BAND_ROWS) {
        int rows = DISPLAY_H - y;
        if (rows > DISPLAY_BAND_ROWS) rows = DISPLAY_BAND_ROWS;
        uint16_t *pixels = display_acquire_band();
        const gfx_band_t band = {pixels, 0, y, DISPLAY_W, rows};
        lock_screen_paint(&band);
        display_push(0, y, DISPLAY_W, rows, pixels);
    }
    display_wait_idle();
    display_set_brightness(2);
    lock_policy_t policy;
    lock_policy_init(&policy, now_ms(), gpio_get_level(BOARD_BOOT_BUTTON) == 0);
    ESP_LOGI(TAG, "LOCKED: audio/listening/capture/touch/personality/render disabled; BOOT x5 within 5s to unlock");
    power_locked_begin();
    bool was_bright = false, unlock_pending = false;
    for (;;) {
        const uint32_t now = now_ms();
        bool down = gpio_get_level(BOARD_BOOT_BUTTON) == 0;
        bool unlock = lock_policy_update(&policy, now, down, power_locked_poll(now));
        if (was_bright != policy.bright) {
            display_set_brightness(policy.bright ? 22 : 2);
            was_bright = policy.bright;
        }
        if (unlock) unlock_pending = true;
        /* BOOT is also a ROM strapping pin. Wait for its debounced release
         * before committing/restarting, including after the fifth press. */
        if (unlock_pending && !policy.raw_down && !policy.stable_down) {
            unlock_pending = false;
            if (settings_set_locked(false) == ESP_OK) {
                display_set_brightness(0);
                ESP_LOGI(TAG, "unlocked by five BOOT presses");
                esp_restart();
            }
            /* Failed NVS commit leaves both RAM and persistent state locked. */
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
