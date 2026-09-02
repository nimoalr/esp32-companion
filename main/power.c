#include "power.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "board.h"
#include "imu.h"
#include "pmic.h"

static const char *TAG = "power";

#define SAMPLE_MS_ACTIVE   50
#define SAMPLE_MS_DROWSY   200
#define BATTERY_MS         5000
#define MOTION_HITS        2        /* consecutive samples above threshold */

static power_state_t s_state = POWER_ACTIVE;
static uint32_t s_state_since_ms;
static uint32_t s_last_activity_ms;
static uint32_t s_last_motion_ms;

static esp_pm_lock_handle_t s_lock_cpu;      /* ESP_PM_CPU_FREQ_MAX */
static esp_pm_lock_handle_t s_lock_nosleep;  /* ESP_PM_NO_LIGHT_SLEEP */
static bool s_cpu_held, s_nosleep_held;

static bool s_imu_ok, s_pmic_ok;
static uint32_t s_last_sample_ms, s_last_battery_ms;
static int32_t s_grav[3];
static bool s_grav_init;
static int s_motion_hits;
static pmic_battery_t s_batt;

const char *power_state_name(power_state_t s)
{
    static const char *n[] = { "ACTIVE", "DROWSY", "SLEEP", "DEEP" };
    return (s <= POWER_DEEP) ? n[s] : "?";
}

static void hold_cpu(bool hold)
{
    if (hold && !s_cpu_held) {
        esp_pm_lock_acquire(s_lock_cpu);
    } else if (!hold && s_cpu_held) {
        esp_pm_lock_release(s_lock_cpu);
    }
    s_cpu_held = hold;
}

static void hold_nosleep(bool hold)
{
    if (hold && !s_nosleep_held) {
        esp_pm_lock_acquire(s_lock_nosleep);
    } else if (!hold && s_nosleep_held) {
        esp_pm_lock_release(s_lock_nosleep);
    }
    s_nosleep_held = hold;
}

static void enter(power_state_t s, uint32_t now_ms)
{
    ESP_LOGI(TAG, "%s -> %s after %" PRIu32 " s", power_state_name(s_state), power_state_name(s), (now_ms - s_state_since_ms) / 1000);
    s_state = s;
    s_state_since_ms = now_ms;
    switch (s) {
    case POWER_ACTIVE:
        hold_cpu(true);
        hold_nosleep(true);
        break;
    case POWER_DROWSY:
        /* Locks are managed per frame window by power_allow_light_sleep(). */
        hold_cpu(true);
        hold_nosleep(true);
        break;
    default:
        hold_cpu(false);
        hold_nosleep(false);
        break;
    }
}

esp_err_t power_init(void)
{
    const esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_EYES_CPU_MAX_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };
    ESP_RETURN_ON_ERROR(esp_pm_configure(&pm), TAG, "pm configure");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "eyes_cpu", &s_lock_cpu), TAG, "lock");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "eyes_awake", &s_lock_nosleep), TAG, "lock");

    const gpio_config_t int2 = {
        .pin_bit_mask = BIT64(BOARD_IMU_INT2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int2), TAG, "imu int2 gpio");

    esp_err_t err = imu_init();
    s_imu_ok = err == ESP_OK;
    if (!s_imu_ok) {
        ESP_LOGW(TAG, "IMU unavailable (%s): motion detection off, sleep wakes on touch only", esp_err_to_name(err));
    }
    err = pmic_init();
    s_pmic_ok = err == ESP_OK;
    if (!s_pmic_ok) {
        ESP_LOGW(TAG, "PMIC unavailable (%s): no battery telemetry, DEEP falls back to deep sleep", esp_err_to_name(err));
    }

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_last_activity_ms = now_ms;
    s_state_since_ms = now_ms;
    s_state = POWER_ACTIVE;
    hold_cpu(true);
    hold_nosleep(true);
    ESP_LOGI(TAG, "cpu %d MHz max, active timeout %d s, drowsy min %d s, sleep->deep %d s, deep = %s",
             CONFIG_EYES_CPU_MAX_MHZ, CONFIG_EYES_ACTIVE_TIMEOUT_S, CONFIG_EYES_DROWSY_MIN_S, CONFIG_EYES_SLEEP_TO_DEEP_S,
#if CONFIG_EYES_DEEP_PMIC_OFF
             "PMIC off");
#else
             "deep sleep");
#endif
    return ESP_OK;
}

/* Deviation of the current sample from the slow gravity estimate, in mg (+-8 g range: raw / 4.096). */
static void sample_motion(uint32_t now_ms)
{
    int16_t a[3];
    if (imu_read_accel(a) != ESP_OK) {
        return;
    }
    if (!s_grav_init) {
        for (int i = 0; i < 3; i++) s_grav[i] = a[i];
        s_grav_init = true;
        return;
    }
    int32_t dev_max = 0;
    for (int i = 0; i < 3; i++) {
        int32_t d = a[i] - s_grav[i];
        if (d < 0) d = -d;
        if (d > dev_max) dev_max = d;
        s_grav[i] += (a[i] - s_grav[i]) >> 3;
    }
    const int32_t dev_mg = (dev_max * 1000) >> 12;
    if (dev_mg >= CONFIG_EYES_MOTION_MG) {
        if (++s_motion_hits >= MOTION_HITS) {
            s_last_motion_ms = now_ms;
            s_last_activity_ms = now_ms;
        }
    } else {
        s_motion_hits = 0;
    }
}

power_state_t power_update(uint32_t now_ms, uint32_t touch_ms)
{
    if (touch_ms && (int32_t)(touch_ms - s_last_activity_ms) > 0) {
        s_last_activity_ms = touch_ms;
    }

    const uint32_t sample_ms = (s_state == POWER_ACTIVE) ? SAMPLE_MS_ACTIVE : SAMPLE_MS_DROWSY;
    if (s_imu_ok && now_ms - s_last_sample_ms >= sample_ms) {
        s_last_sample_ms = now_ms;
        sample_motion(now_ms);
    }
    if (s_pmic_ok && now_ms - s_last_battery_ms >= BATTERY_MS) {
        s_last_battery_ms = now_ms;
        pmic_read_battery(&s_batt);
    }

#if CONFIG_EYES_POWER_ENABLE
    const uint32_t idle_ms = now_ms - s_last_activity_ms;
    switch (s_state) {
    case POWER_ACTIVE:
        if (idle_ms >= CONFIG_EYES_ACTIVE_TIMEOUT_S * 1000u) {
            enter(POWER_DROWSY, now_ms);
        }
        break;
    case POWER_DROWSY:
        if ((int32_t)(s_last_activity_ms - s_state_since_ms) > 0) {
            enter(POWER_ACTIVE, now_ms);
        } else if (now_ms - s_state_since_ms >= CONFIG_EYES_DROWSY_MIN_S * 1000u) {
            enter(POWER_SLEEP, now_ms);
        }
        break;
    default:
        break;
    }
#endif
    return s_state;
}

power_state_t power_state(void)
{
    return s_state;
}

void power_allow_light_sleep(bool allow)
{
    /* DROWSY frame window: full clock and no sleep while rasterising and pushing
     * (race to idle), then release everything until the next frame slot. */
    if (s_state == POWER_DROWSY) {
        hold_cpu(!allow);
        hold_nosleep(!allow);
    }
}

power_wake_t power_light_sleep(uint32_t max_ms)
{
    if (s_imu_ok) {
        if (imu_enter_wom((uint8_t)CONFIG_EYES_WOM_MG) != ESP_OK) {
            ESP_LOGW(TAG, "could not arm wake-on-motion");
        }
    }

    /* Touch INT: edge ISR off, level wake on. IMU INT2: level wake on. */
    gpio_intr_disable(BOARD_TOUCH_INT);
    gpio_wakeup_enable(BOARD_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    if (s_imu_ok) {
        gpio_wakeup_enable(BOARD_IMU_INT2, GPIO_INTR_HIGH_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup((uint64_t)max_ms * 1000ULL);

    hold_cpu(false);
    hold_nosleep(false);
    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = esp_light_sleep_start();
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const int64_t slept_ms = (esp_timer_get_time() - t0) / 1000;

    /* Undo the wake configuration and give the touch pin its edge interrupt back. */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    gpio_wakeup_disable(BOARD_TOUCH_INT);
    if (s_imu_ok) {
        gpio_wakeup_disable(BOARD_IMU_INT2);
    }
    gpio_set_intr_type(BOARD_TOUCH_INT, GPIO_INTR_NEGEDGE);
    gpio_intr_enable(BOARD_TOUCH_INT);

    power_wake_t wake;
    if (err != ESP_OK) {
        /* ESP_ERR_SLEEP_REJECT: a wake pin was already active when we tried to sleep. */
        wake = gpio_get_level(BOARD_IMU_INT2) ? POWER_WAKE_MOTION : POWER_WAKE_TOUCH;
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        wake = POWER_WAKE_TIMEOUT;
    } else if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        wake = (s_imu_ok && gpio_get_level(BOARD_IMU_INT2)) ? POWER_WAKE_MOTION : POWER_WAKE_TOUCH;
    } else {
        wake = POWER_WAKE_OTHER;
    }
    ESP_LOGI(TAG, "light sleep %lld ms, wake: %s (cause %d, %s)", (long long)slept_ms,
             wake == POWER_WAKE_MOTION ? "motion" : wake == POWER_WAKE_TOUCH ? "touch" :
             wake == POWER_WAKE_TIMEOUT ? "timeout" : "other", (int)cause, esp_err_to_name(err));

    if (s_imu_ok && wake != POWER_WAKE_TIMEOUT) {
        if (imu_init() != ESP_OK) {
            ESP_LOGW(TAG, "IMU re-init after sleep failed");
        }
        s_grav_init = false;
        s_motion_hits = 0;
    }
    return wake;
}

void power_wake_to_active(uint32_t now_ms)
{
    s_last_activity_ms = now_ms;
    s_last_motion_ms = now_ms;
    enter(POWER_ACTIVE, now_ms);
}

void power_enter_deep(void)
{
    ESP_LOGW(TAG, "entering DEEP");
#if CONFIG_EYES_DEEP_PMIC_OFF
    if (s_pmic_ok) {
        if (s_imu_ok) {
            imu_power_down();
        }
        if (pmic_power_off() == ESP_OK) {
            /* Rails collapse within milliseconds; nothing to do but wait for it. */
            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGE(TAG, "still running after PMIC power off");
            }
        }
        ESP_LOGE(TAG, "PMIC power off failed, falling back to deep sleep");
    }
#endif
    /* ESP32 deep sleep: rails stay up, the IMU's wake-on-motion on GPIO21 restarts the chip. */
    if (s_imu_ok && imu_enter_wom((uint8_t)CONFIG_EYES_WOM_MG) == ESP_OK) {
        esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_IMU_INT2), ESP_EXT1_WAKEUP_ANY_HIGH);
    } else {
        /* No wake source available: wake once a day so the board is not lost for good. */
        esp_sleep_enable_timer_wakeup(24ULL * 3600ULL * 1000000ULL);
    }
    esp_deep_sleep_start();
}

void power_battery(pmic_battery_t *out)
{
    *out = s_batt;
}

bool power_motion_recent(uint32_t now_ms, uint32_t window_ms)
{
    return s_last_motion_ms && (now_ms - s_last_motion_ms) <= window_ms;
}
