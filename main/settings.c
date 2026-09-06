#include "settings.h"

#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
#define NS "companion"

settings_t g_settings;

/*
 * Named after what they are, not after machine-learning puns like Vector's. The
 * first entry is the orange the character has always had; the others sit at
 * lightness 50-60 % so the expression tints have room in both directions.
 */
const eye_palette_t k_eye_palette[EYE_PALETTE_N] = {
    { "Orange",   0xFF8C00 },
    { "Amber",    0xFFC000 },
    { "Lime",     0x9BE800 },
    { "Teal",     0x00E0B0 },
    { "Sapphire", 0x2A7DFF },
    { "Violet",   0xA060FF },
    { "Pink",     0xFF5FA8 },
    { "Ivory",    0xF2E2C4 },
};

uint32_t settings_eye_rgb(void)
{
    return k_eye_palette[g_settings.eye_color < EYE_PALETTE_N ? g_settings.eye_color : 0].rgb;
}

esp_err_t settings_init(void)
{
    g_settings.brightness_active = CONFIG_EYES_BRIGHTNESS_ACTIVE;
    g_settings.brightness_aod = CONFIG_EYES_BRIGHTNESS_AOD;
    imu_cal_default(&g_settings.cal);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing (%s)", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");

    nvs_handle_t h;
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no stored settings, using defaults (first boot)");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs open");

    uint8_t v;
    if (nvs_get_u8(h, "bri_act", &v) == ESP_OK && v >= 1 && v <= 100) g_settings.brightness_active = v;
    if (nvs_get_u8(h, "bri_aod", &v) == ESP_OK && v >= 1 && v <= 100) g_settings.brightness_aod = v;
    if (nvs_get_u8(h, "eye_col", &v) == ESP_OK && v < EYE_PALETTE_N) g_settings.eye_color = v;
    imu_cal_t cal;
    size_t len = sizeof(cal);
    if (nvs_get_blob(h, "cal2", &cal, &len) == ESP_OK && len == sizeof(cal) && cal.valid) {
        g_settings.cal = cal;
    }
    mic_cal_t mic;
    len = sizeof(mic);
    if (nvs_get_blob(h, "mic1", &mic, &len) == ESP_OK && len == sizeof(mic) && mic.valid) {
        g_settings.mic = mic;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: brightness %u%% / %u%%, accel calibration %s, mic calibration %s", g_settings.brightness_active,
             g_settings.brightness_aod, g_settings.cal.valid ? "present" : "absent", g_settings.mic.valid ? "present" : "absent");
    if (g_settings.mic.valid) {
        ESP_LOGI(TAG, "mic axis: offset %+.2f gain %+.2f (ends %.2f samples apart)", g_settings.mic.offset,
                 g_settings.mic.gain, g_settings.mic.sep);
    }
    return ESP_OK;
}

esp_err_t settings_save(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "nvs open");
    esp_err_t err = ESP_OK;
    if (err == ESP_OK) err = nvs_set_u8(h, "bri_act", g_settings.brightness_active);
    if (err == ESP_OK) err = nvs_set_u8(h, "bri_aod", g_settings.brightness_aod);
    if (err == ESP_OK) err = nvs_set_u8(h, "eye_col", g_settings.eye_color);
    if (err == ESP_OK) err = nvs_set_blob(h, "cal2", &g_settings.cal, sizeof(g_settings.cal));
    if (err == ESP_OK) err = nvs_set_blob(h, "mic1", &g_settings.mic, sizeof(g_settings.mic));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_RETURN_ON_ERROR(err, TAG, "save");
    ESP_LOGI(TAG, "saved");
    return ESP_OK;
}
