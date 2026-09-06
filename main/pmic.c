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

    /* Rail map, for the record: DCDC enable 0x80, LDO enables 0x90/0x91, ALDO1-4 voltage 0x92-0x95, BLDO1-2 0x96-0x97 */
    {
        uint8_t dc = 0, l0 = 0, l1 = 0, v[6] = { 0 };
        rd(0x80, &dc); rd(0x90, &l0); rd(0x91, &l1);
        for (int i = 0; i < 6; i++) rd((uint8_t)(0x92 + i), &v[i]);
        ESP_LOGI(TAG, "rails: DCDC en 0x%02X, LDO en 0x%02X 0x%02X; ALDO1-4 %u %u %u %u mV, BLDO1-2 %u %u mV",
                 dc, l0, l1, 500 + 100 * (v[0] & 0x1F), 500 + 100 * (v[1] & 0x1F), 500 + 100 * (v[2] & 0x1F),
                 500 + 100 * (v[3] & 0x1F), 500 + 100 * (v[4] & 0x1F), 500 + 100 * (v[5] & 0x1F));
    }
    /* PWR button: the long-press flag after 2 s (reg 0x27 IRQLEVEL bits 5:4 = 10), the
     * power-off hold untouched; enable the short/long press flags (reg 0x41 bits 3, 2) */
    uint8_t lvl;
    if (rd(0x27, &lvl) == ESP_OK) wr(0x27, (uint8_t)((lvl & ~0x30) | 0x20));
    uint8_t en;
    if (rd(0x41, &en) == ESP_OK) wr(0x41, (uint8_t)(en | 0x0C));
    wr(0x49, 0x0C);                          /* clear stale flags */
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

esp_err_t pmic_poll_key(bool *short_press, bool *long_press)
{
    uint8_t st;
    *short_press = *long_press = false;
    ESP_RETURN_ON_ERROR(rd(0x49, &st), TAG, "irq status");
    const uint8_t keys = st & 0x0C;         /* bit 3 short press, bit 2 long press */
    if (keys) {
        *short_press = (keys & 0x08) != 0;
        *long_press = (keys & 0x04) != 0;
        wr(0x49, keys);                     /* write 1 to clear */
    }
    return ESP_OK;
}

esp_err_t pmic_read_vbus(bool *vbus)
{
    uint8_t s1 = 0;
    ESP_RETURN_ON_ERROR(rd(REG_STATUS1, &s1), TAG, "status1");
    *vbus = (s1 >> 5) & 1;
    return ESP_OK;
}
