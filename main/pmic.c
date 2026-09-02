#include "pmic.h"

#include "esp_check.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "board.h"

static const char *TAG = "pmic";

/* Register map, X-Powers AXP2101 datasheet; names follow XPowersLib. */
#define REG_STATUS1           0x00   /* bit5 VBUS good, bit3 battery present */
#define REG_STATUS2           0x01   /* bits7:5 charge state: 01 charging, 02 discharging */
#define REG_IC_TYPE           0x03   /* 0x4A */
#define REG_COMMON_CONFIG     0x10   /* bit0 soft power off */
#define REG_ADC_CHANNEL_CTRL  0x30   /* bit0 battery voltage ADC, bit1 TS pin */
#define REG_ADC_VBAT_H        0x34   /* 5 high bits */
#define REG_ADC_VBAT_L        0x35
#define REG_BAT_PERCENT       0xA4

#define AXP2101_CHIP_ID       0x4A

static i2c_master_dev_handle_t s_dev;

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 50);
}

esp_err_t pmic_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_add_device(BOARD_PMIC_ADDR, &s_dev), TAG, "device");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(rd(REG_IC_TYPE, &id), TAG, "AXP2101 not responding at 0x%02X", BOARD_PMIC_ADDR);
    if (id != AXP2101_CHIP_ID) {
        ESP_LOGW(TAG, "unexpected IC type 0x%02X (expected 0x%02X), continuing", id, AXP2101_CHIP_ID);
    }

    /*
     * Battery voltage ADC on, TS (thermistor) measurement off. Waveshare's
     * AXP2101 example for this board disables TS because the supplied pouch
     * has no NTC and an open TS input stops the charger.
     */
    uint8_t adc = 0;
    ESP_RETURN_ON_ERROR(rd(REG_ADC_CHANNEL_CTRL, &adc), TAG, "adc ctrl");
    adc = (uint8_t)((adc | 0x01) & ~0x02);
    ESP_RETURN_ON_ERROR(wr(REG_ADC_CHANNEL_CTRL, adc), TAG, "adc ctrl");

    pmic_battery_t b;
    if (pmic_read_battery(&b) == ESP_OK) {
        ESP_LOGI(TAG, "AXP2101 ok: battery %s, %u mV, %d%%, %s%s", b.present ? "present" : "absent",
                 b.mv, b.percent, b.charging ? "charging" : "not charging", b.vbus ? ", USB in" : "");
    }
    return ESP_OK;
}

esp_err_t pmic_read_battery(pmic_battery_t *out)
{
    uint8_t s1, s2, h, l, pct;
    ESP_RETURN_ON_ERROR(rd(REG_STATUS1, &s1), TAG, "status1");
    ESP_RETURN_ON_ERROR(rd(REG_STATUS2, &s2), TAG, "status2");
    out->present = (s1 >> 3) & 1;
    out->vbus = (s1 >> 5) & 1;
    out->charging = ((s2 >> 5) & 0x07) == 0x01;
    out->mv = 0;
    out->percent = -1;
    if (out->present) {
        ESP_RETURN_ON_ERROR(rd(REG_ADC_VBAT_H, &h), TAG, "vbat");
        ESP_RETURN_ON_ERROR(rd(REG_ADC_VBAT_L, &l), TAG, "vbat");
        out->mv = (uint16_t)(((h & 0x1F) << 8) | l);
        ESP_RETURN_ON_ERROR(rd(REG_BAT_PERCENT, &pct), TAG, "percent");
        out->percent = (int8_t)(pct > 100 ? 100 : pct);
    }
    return ESP_OK;
}

esp_err_t pmic_power_off(void)
{
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(rd(REG_COMMON_CONFIG, &v), TAG, "common config");
    ESP_LOGW(TAG, "powering off");
    return wr(REG_COMMON_CONFIG, (uint8_t)(v | 0x01));
}
