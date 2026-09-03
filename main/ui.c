#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- geometry ------------------------------------------------------------ */

#define W        466
#define H        466
#define CX       233
#define CY       233
#define R        233

#define TITLE_Y      52          /* 16x32 title top */
#define RULE_Y       92
#define BODY_X       104         /* left column for lists / body text */
#define BODY_W       258         /* 21 cells of 12 px + marker */
#define LIST_Y       126
#define ROW_H        32
#define HINT_Y       396         /* 12x24 hint top */
#define RING_R       233
#define RING_T       3

#define LEVEL_SCALE  150         /* px per 1 g of lateral tilt */
#define BALL_R       14

#define CAL_SAMPLES  40          /* 2 s at 20 Hz */
#define CAL_MAX_DEV  60          /* mg; more than this restarts the pose */
#define CAL_BAR_CELLS 14

/* ---- colours (RGB565, panel byte order; filled in by ui_init) ------------- */

static struct {
    uint16_t fg, dim, grey, white, bad, good, sel_bg;
} C;

void ui_set_accent(uint32_t rgb)
{
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    C.fg = gfx_rgb(r, g, b);
    C.dim = gfx_rgb(r * 110 / 255, g * 110 / 255, b * 110 / 255);
    C.sel_bg = C.fg;
}

static void colours_init(void)
{
    ui_set_accent(settings_eye_rgb());
    C.grey = gfx_rgb(120, 120, 120);
    C.white = gfx_rgb(230, 230, 230);
    C.bad = gfx_rgb(230, 70, 50);
    C.good = gfx_rgb(80, 210, 110);
    C.sel_bg = C.fg;
}

/* ---- helpers --------------------------------------------------------------- */

static void dirty_add(ui_t *u, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x0 >= x1 || y0 >= y1) return;
    if (u->ndirty < UI_MAX_DIRTY) {
        u->dirty[u->ndirty++] = (ui_rect_t){ x0, y0, x1, y1 };
    } else {
        /* merge into the last one */
        ui_rect_t *d = &u->dirty[UI_MAX_DIRTY - 1];
        if (x0 < d->x0) d->x0 = x0;
        if (y0 < d->y0) d->y0 = y0;
        if (x1 > d->x1) d->x1 = x1;
        if (y1 > d->y1) d->y1 = y1;
    }
}

static void dirty_all(ui_t *u)
{
    u->ndirty = 0;
    dirty_add(u, 0, 0, W, H);
}

static void action(ui_t *u, ui_action_t a)
{
    u->actions |= (uint8_t)(1u << a);
}

static void text_center(const gfx_band_t *b, const gfx_font_t *f, int y, const char *s, uint16_t fg)
{
    gfx_text(b, f, CX - gfx_text_width(f, s) / 2, y, s, fg, GFX_TRANSPARENT);
}

/* Title, rule, outer ring and footer hint shared by every screen. */
static void chrome(const gfx_band_t *b, const char *title, const char *hint)
{
    gfx_ring(b, CX, CY, RING_R, RING_T, 0, 360, C.dim);
    text_center(b, &font_spleen_16x32, TITLE_Y, title, C.fg);
    const int tw = gfx_text_width(&font_spleen_16x32, title);
    gfx_fill(b, CX - tw / 2 - 30, RULE_Y, tw + 60, 2, C.dim);
    if (hint) {
        text_center(b, &font_spleen_12x24, HINT_Y, hint, C.grey);
    }
}

static void goto_screen(ui_t *u, ui_screen_t s, uint32_t now_ms)
{
    u->screen = s;
    u->screen_since_ms = now_ms;
    u->text_ms = 0;
    dirty_all(u);
}

const char *ui_screen_name(ui_screen_t s)
{
    static const char *n[] = { "MENU", "CALIBRATE", "LEVEL", "BRIGHTNESS", "BATTERY", "COLOR" };
    return (s <= UI_SCREEN_COLOR) ? n[s] : "?";
}

/* ---- menu ------------------------------------------------------------------ */

enum { MENU_CALIBRATE, MENU_LEVEL, MENU_BRIGHTNESS, MENU_COLOR, MENU_HOTSPOT, MENU_BATTERY, MENU_DANCE, MENU_EYES, MENU_COUNT };

static const char *menu_label(const ui_t *u, int i, char *buf, int len)
{
    switch (i) {
    case MENU_CALIBRATE:  return u->settings->cal.valid ? "Calibrate accel" : "Calibrate accel  !";
    case MENU_LEVEL:      return "Level";
    case MENU_BRIGHTNESS: snprintf(buf, (size_t)len, "Brightness   %3u%%", u->settings->brightness_active); return buf;
    case MENU_COLOR:      snprintf(buf, (size_t)len, "Eye colour %s", k_eye_palette[u->settings->eye_color % EYE_PALETTE_N].name); return buf;
    case MENU_HOTSPOT:    return u->settings->hotspot ? "Shaded eyes  on" : "Shaded eyes  off";
    case MENU_BATTERY:    return "Battery";
    case MENU_DANCE:      return "Dance mode";
    default:              return "Back to eyes";
    }
}

static void paint_menu(const ui_t *u, const gfx_band_t *b)
{
    chrome(b, "SETUP", "up/down  tap  hold=back");
    char buf[40];
    for (int i = 0; i < MENU_COUNT; i++) {
        const int y = LIST_Y + i * ROW_H;
        const bool sel = i == u->menu_sel;
        if (sel) {
            gfx_fill(b, BODY_X - 8, y, BODY_W + 16, ROW_H, C.sel_bg);
        }
        const uint16_t fg = sel ? 0 : C.fg;
        gfx_text(b, &font_spleen_12x24, BODY_X, y + 4, sel ? ">" : " ", fg, GFX_TRANSPARENT);
        gfx_text(b, &font_spleen_12x24, BODY_X + 24, y + 4, menu_label(u, i, buf, sizeof buf), fg, GFX_TRANSPARENT);
    }
}

static void menu_input(ui_t *u, ui_input_t in, uint32_t now_ms)
{
    switch (in) {
    case UI_IN_UP:
        if (u->menu_sel < MENU_COUNT - 1) u->menu_sel++;
        dirty_add(u, BODY_X - 8, LIST_Y, BODY_X + BODY_W + 8, LIST_Y + MENU_COUNT * ROW_H);
        break;
    case UI_IN_DOWN:
        if (u->menu_sel > 0) u->menu_sel--;
        dirty_add(u, BODY_X - 8, LIST_Y, BODY_X + BODY_W + 8, LIST_Y + MENU_COUNT * ROW_H);
        break;
    case UI_IN_TAP:
        switch (u->menu_sel) {
        case MENU_CALIBRATE:
            u->cal_step = 0;
            u->cal_capturing = false;
            goto_screen(u, UI_SCREEN_CALIBRATE, now_ms);
            break;
        case MENU_LEVEL:
            u->prev_ball_x = u->ball_x = CX;
            u->prev_ball_y = u->ball_y = CY;
            u->level_text[0] = 0;
            goto_screen(u, UI_SCREEN_LEVEL, now_ms);
            break;
        case MENU_BRIGHTNESS:
            goto_screen(u, UI_SCREEN_BRIGHTNESS, now_ms);
            break;
        case MENU_COLOR:
            goto_screen(u, UI_SCREEN_COLOR, now_ms);
            break;
        case MENU_HOTSPOT:
            u->settings->hotspot = !u->settings->hotspot;
            action(u, UI_ACT_HOTSPOT);
            action(u, UI_ACT_SAVE);
            dirty_add(u, BODY_X - 8, LIST_Y, BODY_X + BODY_W + 8, LIST_Y + MENU_COUNT * ROW_H);
            break;
        case MENU_BATTERY:
            goto_screen(u, UI_SCREEN_BATTERY, now_ms);
            break;
        case MENU_DANCE:
            action(u, UI_ACT_DANCE);
            break;
        default:
            action(u, UI_ACT_EXIT);
            break;
        }
        break;
    case UI_IN_LONG:
    case UI_IN_RIGHT:
        action(u, UI_ACT_EXIT);
        break;
    default:
        break;
    }
}

/* ---- calibrate ------------------------------------------------------------- */

static const char *k_pose_title[IMU_CAL_POSES][2] = {
    { "Lay it flat",        "screen facing up" },
    { "Stand it upright",   "USB port down" },
    { "Rest on left edge",  "left side down" },
};

#define CAL_BODY_Y   132
#define CAL_BAR_Y    236
#define CAL_VAL_Y    300

static void paint_calibrate(const ui_t *u, const gfx_band_t *b)
{
    char line[48];
    if (u->cal_step < IMU_CAL_POSES) {
        snprintf(line, sizeof line, "CALIBRATE %d/%d", u->cal_step + 1, IMU_CAL_POSES);
        chrome(b, line, u->cal_capturing ? "hold still" : (u->first_boot ? "tap when ready" : "tap=start  hold=back"));
        text_center(b, &font_spleen_12x24, CAL_BODY_Y, k_pose_title[u->cal_step][0], C.white);
        text_center(b, &font_spleen_12x24, CAL_BODY_Y + 30, k_pose_title[u->cal_step][1], C.grey);

        /* progress bar: [##########....] */
        const int cells = u->cal_capturing ? u->cal_bar_cells : 0;
        char bar[CAL_BAR_CELLS + 3];
        bar[0] = '[';
        for (int i = 0; i < CAL_BAR_CELLS; i++) bar[1 + i] = i < cells ? '#' : '.';
        bar[CAL_BAR_CELLS + 1] = ']';
        bar[CAL_BAR_CELLS + 2] = 0;
        text_center(b, &font_spleen_16x32, CAL_BAR_Y, bar, u->cal_capturing ? C.fg : C.dim);

        if (u->cal_capturing && u->cal_moved_ms) {
            text_center(b, &font_spleen_12x24, CAL_VAL_Y, "moved, restarting", C.bad);
        } else {
            text_center(b, &font_spleen_8x16, CAL_VAL_Y + 4, u->cal_msg, C.grey);
        }
    } else {
        chrome(b, "CALIBRATE", u->cal_ok ? "tap=save" : "tap=retry  hold=back");
        if (u->cal_ok) {
            text_center(b, &font_spleen_16x32, CAL_BODY_Y, "DONE", C.good);
            snprintf(line, sizeof line, "bias %+d %+d %+d", u->cal_result.bias[0], u->cal_result.bias[1], u->cal_result.bias[2]);
            text_center(b, &font_spleen_12x24, CAL_BODY_Y + 56, line, C.white);
            snprintf(line, sizeof line, "scale %.3f %.3f %.3f",
                     u->cal_result.scale_q16[0] * IMU_CAL_ONE_G_RAW / 65536.f,
                     u->cal_result.scale_q16[1] * IMU_CAL_ONE_G_RAW / 65536.f,
                     u->cal_result.scale_q16[2] * IMU_CAL_ONE_G_RAW / 65536.f);
            text_center(b, &font_spleen_8x16, CAL_BODY_Y + 92, line, C.grey);
            text_center(b, &font_spleen_8x16, CAL_BODY_Y + 112, "raw units, 1 g = 4096", C.grey);
        } else {
            text_center(b, &font_spleen_16x32, CAL_BODY_Y, "FAILED", C.bad);
            /* wrap the reason over two lines of 26 cells */
            const int n = (int)strlen(u->cal_msg);
            char l1[28] = { 0 }, l2[28] = { 0 };
            if (n <= 26) {
                snprintf(l1, sizeof l1, "%.26s", u->cal_msg);
            } else {
                int cut = 26;
                while (cut > 10 && u->cal_msg[cut] != ' ') cut--;
                snprintf(l1, sizeof l1, "%.*s", cut, u->cal_msg);
                snprintf(l2, sizeof l2, "%.26s", u->cal_msg + cut + 1);
            }
            text_center(b, &font_spleen_12x24, CAL_BODY_Y + 56, l1, C.white);
            text_center(b, &font_spleen_12x24, CAL_BODY_Y + 86, l2, C.white);
        }
    }
}

static void cal_begin_capture(ui_t *u, uint32_t now_ms)
{
    imu_cal_capture_begin(&u->cal_cap, CAL_SAMPLES, CAL_MAX_DEV);
    u->cal_capturing = true;
    u->cal_bar_cells = 0;
    u->cal_moved_ms = 0;
    u->cal_msg[0] = 0;
    dirty_all(u);
    (void)now_ms;
}

static void calibrate_input(ui_t *u, ui_input_t in, uint32_t now_ms)
{
    if (in == UI_IN_LONG || in == UI_IN_RIGHT) {
        if (u->first_boot) return;              /* first boot must finish the wizard */
        goto_screen(u, UI_SCREEN_MENU, now_ms);
        return;
    }
    if (in != UI_IN_TAP) return;
    if (u->cal_step < IMU_CAL_POSES) {
        if (!u->cal_capturing) cal_begin_capture(u, now_ms);
    } else if (u->cal_ok) {
        u->settings->cal = u->cal_result;
        action(u, UI_ACT_SAVE);
        if (u->first_boot) {
            u->first_boot = false;
            action(u, UI_ACT_EXIT);
        } else {
            goto_screen(u, UI_SCREEN_MENU, now_ms);
        }
    } else {
        u->cal_step = 0;
        u->cal_capturing = false;
        goto_screen(u, UI_SCREEN_CALIBRATE, now_ms);
    }
}

static void calibrate_update(ui_t *u, uint32_t now_ms, const ui_sensors_t *s)
{
    if (u->cal_step >= IMU_CAL_POSES) return;

    if (s->have_accel && s->accel_ms != u->cal_last_sample_ms) {
        u->cal_last_sample_ms = s->accel_ms;
        char msg[48];
        snprintf(msg, sizeof msg, "x %+6d  y %+6d  z %+6d", s->accel[0], s->accel[1], s->accel[2]);
        if (strcmp(msg, u->cal_msg) != 0 && !(u->cal_capturing && u->cal_moved_ms)) {
            snprintf(u->cal_msg, sizeof u->cal_msg, "%s", msg);
            dirty_add(u, 40, CAL_VAL_Y, W - 40, CAL_VAL_Y + 28);
        }
        if (u->cal_capturing) {
            const bool done = imu_cal_capture_add(&u->cal_cap, s->accel);
            if (u->cal_cap.restarted) {
                u->cal_moved_ms = now_ms;
                dirty_add(u, 40, CAL_VAL_Y, W - 40, CAL_VAL_Y + 28);
            }
            const int cells = (u->cal_cap.count * CAL_BAR_CELLS) / CAL_SAMPLES;
            if (cells != u->cal_bar_cells) {
                u->cal_bar_cells = cells;
                dirty_add(u, 80, CAL_BAR_Y, W - 80, CAL_BAR_Y + 32);
            }
            if (done) {
                imu_cal_capture_mean(&u->cal_cap, u->cal_mean[u->cal_step]);
                u->cal_capturing = false;
                u->cal_step++;
                if (u->cal_step == IMU_CAL_POSES) {
                    char err[48];
                    u->cal_ok = imu_cal_compute(u->cal_mean, &u->cal_result, err, sizeof err);
                    if (!u->cal_ok) snprintf(u->cal_msg, sizeof u->cal_msg, "%s", err);
                }
                dirty_all(u);
            }
        }
    }
    if (u->cal_moved_ms && now_ms - u->cal_moved_ms > 800) {
        u->cal_moved_ms = 0;
        dirty_add(u, 40, CAL_VAL_Y, W - 40, CAL_VAL_Y + 28);
    }
    if (!s->have_accel && u->cal_msg[0] == 0) {
        strcpy(u->cal_msg, "no accelerometer data");
        dirty_add(u, 40, CAL_VAL_Y, W - 40, CAL_VAL_Y + 28);
    }
}

/* ---- level ----------------------------------------------------------------- */

#define LEVEL_TEXT_Y 356

static void paint_level(const ui_t *u, const gfx_band_t *b)
{
    chrome(b, "LEVEL", "hold=back");
    /* cross hairs and reference rings */
    gfx_fill(b, CX - 120, CY, 240, 1, C.dim);
    gfx_fill(b, CX, CY - 120, 1, 240, C.dim);
    gfx_ring(b, CX, CY, 60, 1, 0, 360, C.dim);
    gfx_ring(b, CX, CY, 120, 1, 0, 360, C.dim);
    gfx_disc(b, u->ball_x, u->ball_y, BALL_R, (abs(u->ball_x - CX) < 6 && abs(u->ball_y - CY) < 6) ? C.good : C.fg);
    text_center(b, &font_spleen_12x24, LEVEL_TEXT_Y, u->level_text, C.white);
}

static void level_update(ui_t *u, uint32_t now_ms, const ui_sensors_t *s)
{
    if (!s->have_accel) {
        if (u->level_text[0] == 0) {
            strcpy(u->level_text, "no accelerometer");
            dirty_add(u, 60, LEVEL_TEXT_Y, W - 60, LEVEL_TEXT_Y + 24);
        }
        return;
    }
    if (s->accel_ms == u->cal_last_sample_ms) return;
    u->cal_last_sample_ms = s->accel_ms;

    float g[3], sg[3], bx, by, pitch, roll;
    imu_cal_apply(&u->settings->cal, s->accel, g);
    imu_cal_screen(&u->settings->cal, g, sg);
    imu_cal_tilt(sg, &bx, &by, &pitch, &roll);
    if (bx > 1.f) bx = 1.f;
    if (bx < -1.f) bx = -1.f;
    if (by > 1.f) by = 1.f;
    if (by < -1.f) by = -1.f;
    int nx = CX + (int)(bx * LEVEL_SCALE), ny = CY + (int)(by * LEVEL_SCALE);
    /* smooth a little */
    nx = (u->ball_x * 3 + nx) / 4;
    ny = (u->ball_y * 3 + ny) / 4;
    if (nx != u->ball_x || ny != u->ball_y) {
        const int x0 = (nx < u->ball_x ? nx : u->ball_x) - BALL_R - 2;
        const int x1 = (nx > u->ball_x ? nx : u->ball_x) + BALL_R + 3;
        const int y0 = (ny < u->ball_y ? ny : u->ball_y) - BALL_R - 2;
        const int y1 = (ny > u->ball_y ? ny : u->ball_y) + BALL_R + 3;
        dirty_add(u, x0, y0, x1, y1);
        u->ball_x = nx;
        u->ball_y = ny;
    }
    if (now_ms - u->text_ms >= 200) {
        u->text_ms = now_ms;
        char t[32];
        snprintf(t, sizeof t, "P %+5.1f\xC2\xB0  R %+5.1f\xC2\xB0", pitch, roll);
        if (strcmp(t, u->level_text) != 0) {
            strcpy(u->level_text, t);
            dirty_add(u, 60, LEVEL_TEXT_Y, W - 60, LEVEL_TEXT_Y + 24);
        }
    }
}

/* ---- brightness ------------------------------------------------------------ */

#define BRI_BAR_Y 200

static void paint_brightness(const ui_t *u, const gfx_band_t *b)
{
    chrome(b, "BRIGHTNESS", "tap=cycle  hold=back");
    char line[40];
    snprintf(line, sizeof line, "%3u%%", u->settings->brightness_active);
    text_center(b, &font_spleen_16x32, 136, line, C.white);
    const int cells = (u->settings->brightness_active * 20 + 50) / 100;
    char bar[24];
    bar[0] = '[';
    for (int i = 0; i < 20; i++) bar[1 + i] = i < cells ? '#' : '.';
    bar[21] = ']';
    bar[22] = 0;
    text_center(b, &font_spleen_12x24, BRI_BAR_Y, bar, C.fg);
    snprintf(line, sizeof line, "always-on dim: %u%%", u->settings->brightness_aod);
    text_center(b, &font_spleen_12x24, 260, line, C.grey);
}

static void brightness_input(ui_t *u, ui_input_t in, uint32_t now_ms)
{
    if (in == UI_IN_TAP || in == UI_IN_UP || in == UI_IN_DOWN) {
        static const uint8_t steps[] = { 25, 50, 75, 100 };
        int idx = 0;
        for (int i = 0; i < 4; i++) if (steps[i] <= u->settings->brightness_active) idx = i;
        if (in == UI_IN_DOWN) idx = (idx + 3) % 4; else idx = (idx + 1) % 4;
        u->settings->brightness_active = steps[idx];
        action(u, UI_ACT_BRIGHTNESS);
        dirty_add(u, 60, 130, W - 60, 290);
    } else if (in == UI_IN_LONG || in == UI_IN_RIGHT) {
        action(u, UI_ACT_SAVE);
        goto_screen(u, UI_SCREEN_MENU, now_ms);
    }
}

/* ---- eye colour ------------------------------------------------------------ */

#define COL_EYE_Y  205

static void paint_color(const ui_t *u, const gfx_band_t *b)
{
    chrome(b, "EYE COLOUR", "tap=next  hold=back");
    const eye_palette_t *pal = &k_eye_palette[u->settings->eye_color % EYE_PALETTE_N];
    const uint16_t c = gfx_rgb((pal->rgb >> 16) & 0xFF, (pal->rgb >> 8) & 0xFF, pal->rgb & 0xFF);
    /* a pair of resting eyes in the candidate colour */
    gfx_rrect(b, CX - 95 - 40, COL_EYE_Y - 52, 80, 104, 36, c);
    gfx_rrect(b, CX + 95 - 40, COL_EYE_Y - 52, 80, 104, 36, c);
    char line[40];
    snprintf(line, sizeof line, "%d/%d  %s", u->settings->eye_color % EYE_PALETTE_N + 1, EYE_PALETTE_N, pal->name);
    text_center(b, &font_spleen_16x32, 296, line, C.white);
}

static void color_input(ui_t *u, ui_input_t in, uint32_t now_ms)
{
    if (in == UI_IN_TAP || in == UI_IN_UP || in == UI_IN_DOWN) {
        const int n = EYE_PALETTE_N;
        u->settings->eye_color = (uint8_t)((u->settings->eye_color + (in == UI_IN_DOWN ? n - 1 : 1)) % n);
        ui_set_accent(k_eye_palette[u->settings->eye_color].rgb);
        action(u, UI_ACT_COLOR);
        dirty_all(u);                     /* the accent changed too */
    } else if (in == UI_IN_LONG || in == UI_IN_RIGHT) {
        action(u, UI_ACT_SAVE);
        goto_screen(u, UI_SCREEN_MENU, now_ms);
    }
}

/* ---- battery --------------------------------------------------------------- */

static void paint_battery(const ui_t *u, const gfx_band_t *b)
{
    chrome(b, "BATTERY", "hold=back");
    text_center(b, &font_spleen_16x32, 136, u->text_a, C.white);
    text_center(b, &font_spleen_12x24, 196, u->text_b, C.fg);
    text_center(b, &font_spleen_12x24, 236, u->text_c, C.grey);
    /* gauge on the outer ring: filled arc proportional to charge */
    int pct = 0;
    if (sscanf(u->text_a, "%d", &pct) != 1) pct = 0;
    gfx_ring(b, CX, CY, RING_R - 6, 5, -150, -150 + (300 * pct) / 100, C.fg);
}

static void battery_update(ui_t *u, uint32_t now_ms, const ui_sensors_t *s)
{
    if (u->text_ms && now_ms - u->text_ms < 1000) return;
    u->text_ms = now_ms;
    char a[40], bb[40], c[40];
    if (s->batt_pct >= 0) {
        snprintf(a, sizeof a, "%d%%", s->batt_pct);
        snprintf(bb, sizeof bb, "%u.%03u V", s->batt_mv / 1000, s->batt_mv % 1000);
    } else {
        snprintf(a, sizeof a, "--");
        snprintf(bb, sizeof bb, "no battery");
    }
    snprintf(c, sizeof c, "%s%s", s->charging ? "charging" : "discharging", s->usb ? ", USB" : "");
    if (strcmp(a, u->text_a) || strcmp(bb, u->text_b) || strcmp(c, u->text_c)) {
        strcpy(u->text_a, a);
        strcpy(u->text_b, bb);
        strcpy(u->text_c, c);
        dirty_all(u);   /* the ring gauge spans the screen */
    }
}

/* ---- public ---------------------------------------------------------------- */

void ui_init(ui_t *u, settings_t *s, bool first_boot, uint32_t now_ms)
{
    memset(u, 0, sizeof(*u));
    colours_init();
    u->settings = s;
    u->first_boot = first_boot;
    u->last_input_ms = now_ms;
    u->menu_sel = first_boot ? MENU_CALIBRATE : MENU_EYES;
    if (first_boot) {
        u->cal_step = 0;
        goto_screen(u, UI_SCREEN_CALIBRATE, now_ms);
    } else {
        goto_screen(u, UI_SCREEN_MENU, now_ms);
    }
}

void ui_input(ui_t *u, ui_input_t in, uint32_t now_ms)
{
    u->last_input_ms = now_ms;
    switch (u->screen) {
    case UI_SCREEN_MENU:       menu_input(u, in, now_ms); break;
    case UI_SCREEN_CALIBRATE:  calibrate_input(u, in, now_ms); break;
    case UI_SCREEN_BRIGHTNESS: brightness_input(u, in, now_ms); break;
    case UI_SCREEN_COLOR:      color_input(u, in, now_ms); break;
    case UI_SCREEN_LEVEL:
    case UI_SCREEN_BATTERY:
        if (in == UI_IN_LONG || in == UI_IN_RIGHT) goto_screen(u, UI_SCREEN_MENU, now_ms);
        break;
    }
}

int ui_update(ui_t *u, uint32_t now_ms, const ui_sensors_t *sens, ui_rect_t out[UI_MAX_DIRTY])
{
    switch (u->screen) {
    case UI_SCREEN_CALIBRATE: calibrate_update(u, now_ms, sens); break;
    case UI_SCREEN_LEVEL:     level_update(u, now_ms, sens); break;
    case UI_SCREEN_BATTERY:   battery_update(u, now_ms, sens); break;
    default: break;
    }
    const int n = u->ndirty;
    for (int i = 0; i < n; i++) out[i] = u->dirty[i];
    u->ndirty = 0;
    return n;
}

void ui_paint(const ui_t *u, const gfx_band_t *band)
{
    switch (u->screen) {
    case UI_SCREEN_MENU:       paint_menu(u, band); break;
    case UI_SCREEN_CALIBRATE:  paint_calibrate(u, band); break;
    case UI_SCREEN_LEVEL:      paint_level(u, band); break;
    case UI_SCREEN_BRIGHTNESS: paint_brightness(u, band); break;
    case UI_SCREEN_COLOR:      paint_color(u, band); break;
    case UI_SCREEN_BATTERY:    paint_battery(u, band); break;
    }
}

ui_action_t ui_take_action(ui_t *u)
{
    for (int a = 1; a < 8; a++) {
        if (u->actions & (1u << a)) {
            u->actions &= (uint8_t)~(1u << a);
            return (ui_action_t)a;
        }
    }
    return UI_ACT_NONE;
}
