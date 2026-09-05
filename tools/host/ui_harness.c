/* Render every UI screen to PPM with synthetic sensor data. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ui.h"
#include "gfx.h"
#include "settings.h"

settings_t g_settings;
const eye_palette_t k_eye_palette[EYE_PALETTE_N] = {
    { "Orange", 0xFF8C00 }, { "Amber", 0xFFC000 }, { "Lime", 0x9BE800 }, { "Teal", 0x00E0B0 },
    { "Sapphire", 0x2A7DFF }, { "Violet", 0xA060FF }, { "Pink", 0xFF5FA8 }, { "Ivory", 0xF2E2C4 } };
uint32_t settings_eye_rgb(void) { return k_eye_palette[g_settings.eye_color % EYE_PALETTE_N].rgb; }
esp_err_t settings_init(void) { return 0; }
esp_err_t settings_save(void) { printf("[settings] save\n"); return 0; }
uint32_t esp_random(void) { return 1; }

#define W 466
#define H 466
static uint16_t fb[W * H];
static uint16_t band[W * 32];

static void render(ui_t *u) {
    for (int y = 0; y < H; y += 32) {
        int rows = H - y < 32 ? H - y : 32;
        memset(band, 0, sizeof(uint16_t) * W * rows);
        gfx_band_t b = { band, 0, y, W, rows };
        ui_paint(u, &b);
        memcpy(&fb[y * W], band, (size_t)W * rows * 2);
    }
}
static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t s = fb[i], c = (uint16_t)((s >> 8) | (s << 8));
        unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
static void step(ui_t *u, uint32_t *t, ui_sensors_t *s, int frames) {
    ui_rect_t d[UI_MAX_DIRTY];
    for (int i = 0; i < frames; i++) { *t += 50; s->accel_ms = *t; ui_update(u, *t, s, d); }
}

int main(void) {
    ui_t u; uint32_t t = 1000;
    g_settings.brightness_active = 100; g_settings.brightness_aod = 10; imu_cal_default(&g_settings.cal);
    ui_sensors_t s = { .have_accel = true, .accel = { -12, 30, 4090 }, .batt_mv = 3987, .batt_pct = 87, .charging = true, .usb = true };

    /* first boot: calibrate step 1 idle */
    ui_init(&u, &g_settings, true, t); step(&u, &t, &s, 2); render(&u); write_ppm("out/ui_cal1.ppm");
    /* capturing, half way */
    ui_input(&u, UI_IN_TAP, t); step(&u, &t, &s, 21); render(&u); write_ppm("out/ui_cal1_capture.ppm");
    /* finish pose 1, pose 2 (upright: Y = -1g), pose 3 (left edge: X = +1g) */
    step(&u, &t, &s, 25);
    s.accel[0] = 20; s.accel[1] = -4080; s.accel[2] = 15; ui_input(&u, UI_IN_TAP, t); step(&u, &t, &s, 45);
    s.accel[0] = 4100; s.accel[1] = 10; s.accel[2] = -25; ui_input(&u, UI_IN_TAP, t); step(&u, &t, &s, 45);
    render(&u); write_ppm("out/ui_cal_done.ppm");
    printf("cal ok=%d msg='%s' step=%d\n", u.cal_ok, u.cal_msg, u.cal_step);
    ui_input(&u, UI_IN_TAP, t);   /* save */
    ui_action_t a; while ((a = ui_take_action(&u)) != UI_ACT_NONE) printf("action %d\n", a);
    printf("rot: right(%.2f %.2f %.2f) down(%.2f %.2f %.2f) out(%.2f %.2f %.2f)\n",
        g_settings.cal.rot[0][0], g_settings.cal.rot[0][1], g_settings.cal.rot[0][2],
        g_settings.cal.rot[1][0], g_settings.cal.rot[1][1], g_settings.cal.rot[1][2],
        g_settings.cal.rot[2][0], g_settings.cal.rot[2][1], g_settings.cal.rot[2][2]);

    /* menu */
    ui_init(&u, &g_settings, false, t); ui_input(&u, UI_IN_DOWN, t); ui_input(&u, UI_IN_DOWN, t); render(&u); write_ppm("out/ui_menu.ppm");
    /* level: right edge lowered -> "up" tilts left -> sensor X (= screen right) reads -0.3 g */
    ui_input(&u, UI_IN_DOWN, t); ui_input(&u, UI_IN_TAP, t);
    s.accel[0] = -1200; s.accel[1] = 600; s.accel[2] = 3900; step(&u, &t, &s, 30); render(&u); write_ppm("out/ui_level.ppm");
    printf("level text '%s' ball %d,%d\n", u.level_text, u.ball_x, u.ball_y);
    /* menu items are reached from a fresh menu: UP moves the highlight down the list */
    #define IN(k) do { ui_input(&u, k, t); step(&u, &t, &s, 8); } while (0)
    #define OPEN_ITEM(idx) do { ui_init(&u, &g_settings, false, t); step(&u, &t, &s, 8); for (int i_ = 0; i_ < 6 - (idx); i_++) IN(UI_IN_DOWN); IN(UI_IN_TAP); } while (0)
    /* brightness: item 2, three taps -> 75 % */
    OPEN_ITEM(2); IN(UI_IN_TAP); IN(UI_IN_TAP); IN(UI_IN_TAP);
    render(&u); write_ppm("out/ui_brightness.ppm"); printf("brightness now %u\n", g_settings.brightness_active);
    /* eye colour: item 3, two taps -> Lime; the accent follows */
    OPEN_ITEM(3); IN(UI_IN_TAP); IN(UI_IN_TAP);
    render(&u); write_ppm("out/ui_color.ppm"); printf("eye colour now %u screen %s\n", g_settings.eye_color, ui_screen_name(u.screen));
    IN(UI_IN_LONG); render(&u); write_ppm("out/ui_menu_lime.ppm");
    g_settings.eye_color = 0; ui_set_accent(settings_eye_rgb());
    /* battery: item 4 */
    OPEN_ITEM(4); step(&u, &t, &s, 2); render(&u); write_ppm("out/ui_battery.ppm");
    /* failed calibration: same pose three times */
    ui_input(&u, UI_IN_LONG, t); ui_input(&u, UI_IN_DOWN, t); ui_input(&u, UI_IN_DOWN, t); ui_input(&u, UI_IN_DOWN, t); ui_input(&u, UI_IN_DOWN, t); ui_input(&u, UI_IN_TAP, t);
    s.accel[0] = 0; s.accel[1] = 0; s.accel[2] = 4096;
    for (int p = 0; p < 3; p++) { ui_input(&u, UI_IN_TAP, t); step(&u, &t, &s, 45); }
    render(&u); write_ppm("out/ui_cal_fail.ppm"); printf("fail msg '%s'\n", u.cal_msg);
    return 0;
}
