#include "settings.h"
#include "sdkconfig.h"

settings_t g_settings;

void settings_init(void)
{
    g_settings.brightness_active = CONFIG_EYES_BRIGHTNESS_ACTIVE;
    g_settings.brightness_aod = CONFIG_EYES_BRIGHTNESS_AOD;
}
