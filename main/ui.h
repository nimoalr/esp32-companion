/*
 * Setup UI: a handful of TUI-styled screens laid out for the round display.
 * Pure C on top of gfx.h: no hardware access, so it renders on the host too.
 *
 * The caller feeds gestures and sensor readings, asks which rectangles need
 * repainting, paints them band by band, and executes the actions the UI
 * requests (save settings, apply brightness, leave the UI).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "settings.h"
#include "imu_cal.h"

typedef enum {
    UI_IN_TAP,
    UI_IN_LONG,
    UI_IN_UP,       /* swipe up */
    UI_IN_DOWN,
    UI_IN_LEFT,
    UI_IN_RIGHT,
} ui_input_t;

typedef enum {
    UI_SCREEN_MENU,
    UI_SCREEN_CALIBRATE,
    UI_SCREEN_LEVEL,
    UI_SCREEN_BRIGHTNESS,
    UI_SCREEN_BATTERY,
    UI_SCREEN_COLOR,
    UI_SCREEN_MICCAL,       /* microphone axis wizard: claps from three places */
} ui_screen_t;

typedef enum {
    UI_ACT_NONE = 0,
    UI_ACT_EXIT,            /* leave the UI, back to the eyes */
    UI_ACT_SAVE,            /* settings changed, persist them */
    UI_ACT_BRIGHTNESS,      /* apply g_settings.brightness_active now */
    UI_ACT_DANCE,           /* leave the UI into the dance mode */
    UI_ACT_COLOR,           /* apply g_settings.eye_color now */
    UI_ACT_MICCAL,          /* apply g_settings.mic now */
} ui_action_t;

typedef struct {
    bool have_accel;
    int16_t accel[3];       /* raw */
    uint32_t accel_ms;      /* sample timestamp; the UI only consumes new samples */
    uint16_t batt_mv;
    int8_t batt_pct;        /* -1 unknown */
    bool charging, usb;
    /* learned battery figures (minutes, -1 unknown) */
    int est_left_min, est_full_min, avg_life_min, avg_charge_min;
    int n_discharge, n_charge;
    int run_min;            /* minutes into the current discharge / charge stretch */
    /* microphones (mic wizard) */
    bool mic_on;
    float dir, dir_conf, dir_lag, dir_corr, dir_level_db;
    int dir_peak;
    int mic_rms, mic_rms_l, mic_rms_r;
    int dir_n;
} ui_sensors_t;

typedef struct {
    int x0, y0, x1, y1;     /* [x0, x1) x [y0, y1) */
} ui_rect_t;

#define UI_MAX_DIRTY 4

typedef struct {
    settings_t *settings;
    ui_screen_t screen;
    bool first_boot;
    uint32_t last_input_ms;
    uint32_t screen_since_ms;

    ui_rect_t dirty[UI_MAX_DIRTY];
    int ndirty;
    uint8_t actions;        /* bit mask of ui_action_t */

    /* menu */
    int menu_sel;
    /* calibrate */
    int cal_step;           /* 0..2 pose index, 3 = result */
    bool cal_capturing;
    bool cal_ok;
    char cal_msg[48];
    imu_cal_capture_t cal_cap;
    int32_t cal_mean[IMU_CAL_POSES][3];
    imu_cal_t cal_result;
    uint32_t cal_last_sample_ms;
    uint32_t cal_moved_ms;
    int cal_bar_cells;      /* last drawn progress cells */
    /* level */
    int ball_x, ball_y;     /* screen px */
    int prev_ball_x, prev_ball_y;
    float pitch, roll;
    char level_text[32];
    /* mic wizard */
    int arrow_len;          /* px, signed: + = up */
    int mic_step;           /* 0..2 place index, 3 = result */
    int mic_n;              /* claps collected in the current place */
    int mic_seen_n;         /* transient counter last consumed */
    uint32_t mic_step_ms;   /* when the current place started (claps ignored for a moment) */
    uint32_t mic_clap_ms;   /* last accepted clap */
    uint32_t mic_move_ms;   /* last time the accelerometer saw the device being handled */
    int16_t mic_prev_acc[3];
    uint32_t mic_prev_acc_ms;
    float mic_lag[3][3];
    float mic_db[3][3];
    mic_cal_t mic_result;
    bool mic_ok;
    /* brightness / battery */
    char text_a[40], text_b[40], text_c[40], text_d[64], text_e[64];
    uint32_t text_ms;
} ui_t;

void ui_init(ui_t *u, settings_t *s, bool first_boot, uint32_t now_ms);
/* Log the last mic wizard run (every transient it saw, accepted or not) again: the wizard
 * is usually run on battery, so the run is kept in RAM and replayed once USB is back. */
void ui_miccal_dump(void);
/* The UI's accent colour follows the eye colour; called by ui_init and when the setting changes. */
void ui_set_accent(uint32_t rgb);
void ui_input(ui_t *u, ui_input_t in, uint32_t now_ms);

/* Advance state with fresh sensor data; fills dirty rectangles; returns their count. */
int ui_update(ui_t *u, uint32_t now_ms, const ui_sensors_t *sens, ui_rect_t out[UI_MAX_DIRTY]);

/* Paint the current screen into a (black-cleared) band. Everything is clipped to the band. */
void ui_paint(const ui_t *u, const gfx_band_t *band);

/* Pop pending actions, one at a time; UI_ACT_NONE when empty. */
ui_action_t ui_take_action(ui_t *u);

const char *ui_screen_name(ui_screen_t s);

