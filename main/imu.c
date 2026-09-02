#include "imu.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_bus.h"
#include "board.h"

static const char *TAG = "imu";

/* QMI8658 register map (datasheet rev 1.x; names as in Waveshare/SensorLib). */
#define REG_WHOAMI      0x00    /* 0x05 */
#define REG_CTRL1       0x02    /* bit6 ADDR_AI, bit5 BE, bit4 INT2_EN, bit3 INT1_EN, bit1 sensor disable */
#define REG_CTRL2       0x03    /* bits6:4 accel range, bits3:0 accel ODR */
#define REG_CTRL5       0x06    /* bit0 accel LPF enable, bits2:1 LPF mode */
#define REG_CTRL7       0x08    /* bit0 aEN, bit1 gEN */
#define REG_CTRL8       0x09    /* bit7: use STATUS_INT.bit7 as the CTRL9 handshake */
#define REG_CTRL9       0x0A    /* command register */
#define REG_CAL1_L      0x0B    /* WoM threshold, 1 mg/LSB */
#define REG_CAL1_H      0x0C    /* bits7:6 WoM interrupt select/initial value, bits5:0 blanking samples */
#define REG_STATUS_INT  0x2D    /* bit7 CmdDone */
#define REG_AX_L        0x35    /* AX_L..AZ_H, 6 bytes little-endian */
#define REG_RST_RESULT  0x4D    /* 0x80 after a successful reset */
#define REG_RESET       0x60    /* write 0xB0 */

#define WHOAMI_VAL      0x05
#define RESET_VAL       0xB0
#define RST_RESULT_VAL  0x80

#define CMD_ACK             0x00
#define CMD_WRITE_WOM       0x08

#define ACC_RANGE_8G        (2 << 4)
#define ACC_ODR_62_5HZ      7
#define ACC_ODR_LP_21HZ     13

static i2c_master_dev_handle_t s_dev;

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

static esp_err_t reset_chip(void)
{
    ESP_RETURN_ON_ERROR(wr(REG_RESET, RESET_VAL), TAG, "reset");
    const int64_t t0 = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t v = 0;
        if (rd(REG_RST_RESULT, &v, 1) == ESP_OK && v == RST_RESULT_VAL) {
            break;
        }
        ESP_RETURN_ON_FALSE(esp_timer_get_time() - t0 < 500000, ESP_ERR_TIMEOUT, TAG, "reset did not complete");
    }
    ESP_RETURN_ON_ERROR(wr(REG_CTRL1, 0x40), TAG, "ctrl1");          /* address auto-increment, little-endian */
    ESP_RETURN_ON_ERROR(wr(REG_CTRL8, 0x80), TAG, "ctrl8");          /* CTRL9 handshake on STATUS_INT.7 */
    return ESP_OK;
}

/* CTRL9 command with the STATUS_INT.bit7 handshake. */
static esp_err_t ctrl9(uint8_t cmd)
{
    ESP_RETURN_ON_ERROR(wr(REG_CTRL9, cmd), TAG, "ctrl9");
    uint8_t st = 0;
    int64_t t0 = esp_timer_get_time();
    do {
        vTaskDelay(1);
        ESP_RETURN_ON_ERROR(rd(REG_STATUS_INT, &st, 1), TAG, "status");
        ESP_RETURN_ON_FALSE(esp_timer_get_time() - t0 < 300000, ESP_ERR_TIMEOUT, TAG, "cmd 0x%02X not acknowledged", cmd);
    } while (!(st & 0x80));
    ESP_RETURN_ON_ERROR(wr(REG_CTRL9, CMD_ACK), TAG, "ctrl9 ack");
    t0 = esp_timer_get_time();
    do {
        vTaskDelay(1);
        ESP_RETURN_ON_ERROR(rd(REG_STATUS_INT, &st, 1), TAG, "status");
        ESP_RETURN_ON_FALSE(esp_timer_get_time() - t0 < 300000, ESP_ERR_TIMEOUT, TAG, "cmd 0x%02X ack not cleared", cmd);
    } while (st & 0x80);
    return ESP_OK;
}

esp_err_t imu_init(void)
{
    if (!s_dev) {
        ESP_RETURN_ON_ERROR(i2c_bus_add_device(BOARD_IMU_ADDR, &s_dev), TAG, "device");
    }
    ESP_RETURN_ON_ERROR(reset_chip(), TAG, "reset");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(rd(REG_WHOAMI, &id, 1), TAG, "whoami");
    ESP_RETURN_ON_FALSE(id == WHOAMI_VAL, ESP_ERR_NOT_FOUND, TAG, "WHO_AM_I 0x%02X, expected 0x%02X", id, WHOAMI_VAL);

    ESP_RETURN_ON_ERROR(wr(REG_CTRL7, 0x00), TAG, "ctrl7");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL2, ACC_RANGE_8G | ACC_ODR_62_5HZ), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL5, 0x01), TAG, "ctrl5");          /* accel LPF, mode 0 */
    ESP_RETURN_ON_ERROR(wr(REG_CTRL7, 0x01), TAG, "ctrl7");          /* accel on, gyro off */
    return ESP_OK;
}

esp_err_t imu_read_accel(int16_t xyz[3])
{
    uint8_t b[6];
    ESP_RETURN_ON_ERROR(rd(REG_AX_L, b, 6), TAG, "read");
    xyz[0] = (int16_t)((b[1] << 8) | b[0]);
    xyz[1] = (int16_t)((b[3] << 8) | b[2]);
    xyz[2] = (int16_t)((b[5] << 8) | b[4]);
    return ESP_OK;
}

esp_err_t imu_enter_wom(uint8_t threshold_mg)
{
    ESP_RETURN_ON_ERROR(reset_chip(), TAG, "reset");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL7, 0x00), TAG, "ctrl7");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL2, ACC_RANGE_8G | ACC_ODR_LP_21HZ), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(wr(REG_CAL1_L, threshold_mg), TAG, "cal1_l");
    /* 0b01 << 6: INT2, initial level low (goes high on motion); 0x20 blanking samples. */
    ESP_RETURN_ON_ERROR(wr(REG_CAL1_H, (uint8_t)((0x01 << 6) | 0x20)), TAG, "cal1_h");
    ESP_RETURN_ON_ERROR(ctrl9(CMD_WRITE_WOM), TAG, "wom cmd");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL7, 0x01), TAG, "ctrl7");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL1, 0x40 | 0x10), TAG, "ctrl1");   /* + INT2 output enable */
    ESP_LOGI(TAG, "wake-on-motion armed, %u mg, INT2 -> GPIO%d", threshold_mg, BOARD_IMU_INT2);
    return ESP_OK;
}

esp_err_t imu_power_down(void)
{
    ESP_RETURN_ON_ERROR(wr(REG_CTRL7, 0x00), TAG, "ctrl7");
    return wr(REG_CTRL1, 0x40 | 0x02);                                /* sensor disable */
}
