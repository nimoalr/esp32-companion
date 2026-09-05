#include "i2c_bus.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "board.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t s_bus;

/*
 * Bus recovery. A reset that lands in the middle of a read leaves the slave
 * driving SDA low, and no START can be generated until it has been clocked
 * through the rest of its byte. Bit-bang up to 16 SCL pulses with the pins as
 * open-drain GPIOs, then a STOP, before the driver takes the pins.
 */
static void bus_recover(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = BIT64(BOARD_I2C_SDA) | BIT64(BOARD_I2C_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_I2C_SDA, 1);
    gpio_set_level(BOARD_I2C_SCL, 1);
    esp_rom_delay_us(10);
    if (gpio_get_level(BOARD_I2C_SDA)) {
        return;                                 /* bus idle */
    }
    int pulses = 0;
    while (!gpio_get_level(BOARD_I2C_SDA) && pulses < 16) {
        gpio_set_level(BOARD_I2C_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(BOARD_I2C_SCL, 1);
        esp_rom_delay_us(5);
        pulses++;
    }
    /* STOP: SDA low -> high while SCL is high */
    gpio_set_level(BOARD_I2C_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(BOARD_I2C_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(BOARD_I2C_SDA, 1);
    esp_rom_delay_us(5);
    ESP_LOGW(TAG, "SDA was held low at boot: clocked %d pulses, SDA now %s", pulses, gpio_get_level(BOARD_I2C_SDA) ? "high" : "STILL LOW");
}

esp_err_t i2c_bus_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    bus_recover();
    const i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "bus");
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get(void)
{
    return s_bus;
}

esp_err_t i2c_bus_add_device(uint8_t addr, i2c_master_dev_handle_t *dev)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, dev);
}
