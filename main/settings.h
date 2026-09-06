/* Runtime settings and calibration, persisted in NVS. Defaults come from Kconfig. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "imu_cal.h"
#include "audio_features.h"

typedef struct {
    uint8_t brightness_active;   /* percent, ACTIVE state */
    uint8_t brightness_aod;      /* percent, DROWSY (always-on) state */
    uint8_t eye_color;           /* index into k_eye_palette */
    imu_cal_t cal;               /* cal.valid == false until the wizard has run */
    mic_cal_t mic;               /* microphone axis, same rule */
    uint8_t voice_register;      /* 0 low, 1 mid, 2 high */
    uint8_t chattiness;          /* 0 quiet .. 3 talkative */
    uint8_t volume;              /* percent */
} settings_t;

extern settings_t g_settings;

/* Selectable base colours for the eyes. */
typedef struct {
    const char *name;
    uint32_t rgb;                /* 0xRRGGBB */
} eye_palette_t;
#define EYE_PALETTE_N 8
extern const eye_palette_t k_eye_palette[EYE_PALETTE_N];
/* RGB of the configured eye colour. */
uint32_t settings_eye_rgb(void);

/* Initialise NVS and load everything that is stored; missing keys keep their defaults. */
esp_err_t settings_init(void);
esp_err_t settings_save(void);
