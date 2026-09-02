/* Runtime settings. Defaults come from Kconfig; there is no persistence yet. */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t brightness_active;   /* percent, ACTIVE state */
    uint8_t brightness_aod;      /* percent, DROWSY (always-on) state */
} settings_t;

extern settings_t g_settings;

void settings_init(void);
