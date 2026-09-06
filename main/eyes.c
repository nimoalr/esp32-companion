#include "eyes.h"

#include <math.h>
#include <string.h>
#include "esp_random.h"
#include "board.h"

/* Resting geometry: two orange capsules either side of the centre of the 466 px disc. */
#define EYE_W_PX        121                 /* 110 originally; +10 % on 2026-09-06 */
#define EYE_H_PX        154                 /* 140 originally */
#define EYE_R_PX        48
#define EYE_SEP_PX      95          /* centre offset from the screen centre */
#define EYE_CY_PX       (BOARD_LCD_V_RES / 2)
#define EYE_COLOR       0xFF8C00    /* orange */
#define EYE_MIN_H_PX    4           /* a closed eye is still a thin sliver */

/*
 * Blink: 3..10 s apart. The lids close faster than they open and the eye
 * widens as it thins, so the closed eye is a wide sliver; the profile is a
 * scaled version of Vector's blink keyframes.
 */
#define BLINK_MIN_MS    3000
#define BLINK_MAX_MS    10000
typedef struct {
    uint16_t t_ms;
    int32_t h, w;           /* Q16 scales */
} blink_kf_t;
static const blink_kf_t k_blink[] = {
    {   0, Q16(1.00), Q16(1.00) },
    {  35, Q16(0.85), Q16(1.03) },
    {  70, Q16(0.55), Q16(1.10) },
    { 105, Q16(0.10), Q16(1.22) },
    { 140, Q16(0.04), Q16(1.32) },     /* closed */
    { 175, Q16(0.15), Q16(1.22) },
    { 215, Q16(0.55), Q16(1.12) },
    { 265, Q16(0.85), Q16(1.03) },
    { 340, Q16(1.00), Q16(1.00) },
};
#define BLINK_N         ((int)(sizeof(k_blink) / sizeof(k_blink[0])))
#define BLINK_TOTAL_MS  340

/*
 * Gaze darts: every 1..2.25 s the eyes jump to a new spot near the centre.
 * Duration grows with the distance; medium and long darts squash the eyes on
 * the way (a long one is almost a blink), and the two axes move with a lag so
 * the path curves. Long darts sometimes trigger a real blink.
 */
#define DART_MIN_MS     1000
#define DART_MAX_MS     2250
#define DART_MAX_DX_PX  16
#define DART_MAX_DY_PX  11
#define DART_BASE_MS    40
#define DART_MS_PER_PX  3
#define DART_MAX_DUR_MS 130
#define DART_MEDIUM_PX  5
#define DART_LONG_PX    10
#define DART_BLINK_PX   12
#define DART_BLINK_PCT  30
#define DART_SQUASH_MED Q16(0.15)
#define DART_SQUASH_LONG Q16(0.28)

/* Gaze-dependent size (Vector's LookAt): bigger looking up, smaller looking down, the leading eye taller looking sideways. */
#define LOOK_RANGE_PX   24
#define LOOK_UP_GAIN    Q16(0.06)
#define LOOK_DOWN_GAIN  Q16(0.08)
#define LOOK_SIDE_GAIN  Q16(0.045)

/* Hot spot: Gaussian lightness falloff, sigma = HOT_SIGMA x eye height, darkest level 31 - HOT_K at zero. */
#define HOT_SIGMA       Q16(0.50)
#define HOT_K           14
#define HOT_GAZE_GAIN   Q16(1.5)    /* Vector's HotSpotPositionMultiplier */

/* Attention: gaze pull toward the touched point, px of eye travel per px of screen distance. */
#define ATTEND_GAIN     Q16(0.11)
#define ATTEND_MAX_X    24
#define ATTEND_MAX_Y    18
#define ATTEND_DART     Q16(0.07)   /* dart amplitude while attending (Vector: 1 px of 15) */

/* Stretch along fast pose-driven moves, squash across them. */
#define MOTION_GAIN     Q16(0.75)   /* per px/ms */
#define MOTION_MAX      Q16(0.18)
#define MOTION_PERP     Q16(0.6)

static inline int32_t q16_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 16);
}

static inline int32_t q16_div(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a << 16) / b);
}

static inline int32_t clamp_q16(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int32_t ease_in_out_q16(int32_t t)
{
    if (t <= 0) return 0;
    if (t >= Q16_ONE) return Q16_ONE;
    const int64_t tt = ((int64_t)t * t) >> 16;                  /* t^2 */
    return (int32_t)((tt * ((3 << 16) - 2 * (int64_t)t)) >> 16); /* t^2 (3 - 2t) */
}

/* Ease-out with a ~6 % overshoot: 1 + 2.3 u^3 + 1.3 u^2, u = t - 1. */
int32_t ease_out_back_q16(int32_t t)
{
    if (t <= 0) return 0;
    if (t >= Q16_ONE) return Q16_ONE;
    const int32_t u = t - Q16_ONE;
    const int32_t u2 = q16_mul(u, u);
    const int32_t u3 = q16_mul(u2, u);
    return Q16_ONE + q16_mul(Q16(2.3), u3) + q16_mul(Q16(1.3), u2);
}

static uint32_t rng_next(eyes_idle_t *idle)
{
    /* xorshift32 */
    uint32_t x = idle->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    idle->rng = x;
    return x;
}

static uint32_t rng_range(eyes_idle_t *idle, uint32_t lo, uint32_t hi)
{
    return lo + rng_next(idle) % (hi - lo + 1);
}

static void eye_init(EyeState *s, int32_t cx_px, uint32_t now_ms)
{
    memset(s, 0, sizeof(*s));
    s->base.cx = cx_px << 16;
    s->base.cy = EYE_CY_PX << 16;
    s->base.w = EYE_W_PX << 16;
    s->base.h = EYE_H_PX << 16;
    for (int i = 0; i < 4; i++) s->base.rad[i] = s->base.rady[i] = EYE_R_PX << 16;
    s->base.color = EYE_COLOR;
    const eye_pose_t neutral = EYE_POSE_NEUTRAL;
    s->cur = s->from = s->to = neutral;
    for (int i = 0; i < EYE_POSE_FIELDS; i++) {
        s->t0_ms[i] = now_ms;
        s->dur_ms[i] = 1;
    }
    s->prev_cx = s->base.cx;
    s->prev_cy = s->base.cy;
}

/* ---- colour ------------------------------------------------------------------ */

static void rgb_to_hsl(uint32_t rgb, float *h, float *s, float *l)
{
    const float r = (float)((rgb >> 16) & 0xFF) / 255.f, g = (float)((rgb >> 8) & 0xFF) / 255.f, b = (float)(rgb & 0xFF) / 255.f;
    const float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    *l = 0.5f * (mx + mn);
    if (d < 1e-4f) {
        *h = 0.f;
        *s = 0.f;
        return;
    }
    *s = d / (1.f - fabsf(2.f * *l - 1.f));
    float hh;
    if (mx == r) hh = fmodf((g - b) / d, 6.f);
    else if (mx == g) hh = (b - r) / d + 2.f;
    else hh = (r - g) / d + 4.f;
    hh *= 60.f;
    if (hh < 0.f) hh += 360.f;
    *h = hh;
}

static uint32_t hsl_to_rgb(float h, float s, float l)
{
    h = fmodf(h, 360.f);
    if (h < 0.f) h += 360.f;
    s = fminf(fmaxf(s, 0.f), 1.f);
    l = fminf(fmaxf(l, 0.f), 1.f);
    const float c = (1.f - fabsf(2.f * l - 1.f)) * s;
    const float x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f));
    const float m = l - c / 2.f;
    float r, g, b;
    if (h < 60.f)       { r = c; g = x; b = 0; }
    else if (h < 120.f) { r = x; g = c; b = 0; }
    else if (h < 180.f) { r = 0; g = c; b = x; }
    else if (h < 240.f) { r = 0; g = x; b = c; }
    else if (h < 300.f) { r = x; g = 0; b = c; }
    else                { r = c; g = 0; b = x; }
    const uint32_t R = (uint32_t)((r + m) * 255.f + 0.5f), G = (uint32_t)((g + m) * 255.f + 0.5f), B = (uint32_t)((b + m) * 255.f + 0.5f);
    return (R << 16) | (G << 8) | B;
}

void eyes_set_base_color(eyes_t *e, uint32_t rgb)
{
    rgb_to_hsl(rgb & 0xFFFFFF, &e->base_h, &e->base_s, &e->base_l);
}

void eyes_set_tint(eyes_t *e, const eye_tint_t t[2], uint32_t dur_ms, uint32_t now_ms)
{
    for (int i = 0; i < 2; i++) {
        e->tint_from[i] = e->tint_cur[i];
        e->tint_to[i] = t[i];
    }
    e->tint_t0_ms = now_ms;
    e->tint_dur_ms = dur_ms ? dur_ms : 1;
}

void eyes_set_hotspot(eyes_t *e, bool on)
{
    if (e->hot == on) return;
    e->hot = on;
    e->rgb[0] = e->rgb[1] = 0xFFFFFFFF;   /* rebuild the LUTs (lut2 is only built while on) */
}

void eyes_set_face_offset(eyes_t *e, int32_t dx_q16, int32_t dy_q16)
{
    e->face_dx = dx_q16;
    e->face_dy = dy_q16;
}

void eyes_set_face_scale(eyes_t *e, int32_t scale_q16)
{
    e->face_scale = clamp_q16(scale_q16, Q16(0.3), Q16(1.6));
}

void eyes_set_attention(eyes_t *e, bool on, int x_px, int y_px)
{
    e->attend = on;
    if (on) {
        e->attend_tx = clamp_q16(q16_mul((x_px - BOARD_LCD_H_RES / 2) << 16, ATTEND_GAIN), -(ATTEND_MAX_X << 16), ATTEND_MAX_X << 16);
        e->attend_ty = clamp_q16(q16_mul((y_px - BOARD_LCD_V_RES / 2) << 16, ATTEND_GAIN), -(ATTEND_MAX_Y << 16), ATTEND_MAX_Y << 16);
    } else {
        e->attend_tx = e->attend_ty = 0;
    }
}

void eyes_set_fx(eyes_t *e, int fx, float mix)
{
    if (fx && !e->fx) e->rgb[0] = e->rgb[1] = 0xFFFFFFFF;   /* lut2 is only kept fresh while it is needed */
    e->fx = fx;
    e->fx_mix = mix < 0.f ? 0.f : mix > 1.f ? 1.f : mix;
}

void eyes_set_disco(eyes_t *e, float spin, uint32_t seed)
{
    e->disco_spin = spin;
    e->disco_seed = seed;
}

void eyes_set_spots(eyes_t *e, int n, const float *x, const float *y, float r)
{
    e->spots_n = n < 0 ? 0 : n > 3 ? 3 : n;
    for (int i = 0; i < e->spots_n; i++) { e->spot_x[i] = x[i]; e->spot_y[i] = y[i]; }
    e->spot_r = r;
}

void eyes_set_bar_heights(eyes_t *e, int eye, const float h[8])
{
    for (int i = 0; i < 8; i++) e->bar_h[eye][i] = h[i] < 0.f ? 0.f : h[i] > 1.f ? 1.f : h[i];
}

void eyes_set_mood(eyes_t *e, int32_t lum_q16, int32_t sat_q16)
{
    e->mood_lum = lum_q16;
    e->mood_sat = sat_q16;
}

uint32_t eyes_color(const eyes_t *e, int eye)
{
    return e->rgb[eye];
}

/* 32 lightness levels x 64 coverage levels of one colour, panel byte order. */
static void build_lut2(uint16_t *lut2, uint32_t rgb)
{
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    for (int l = 0; l < 32; l++) {
        for (int c = 0; c < 64; c++) {
            const uint32_t k = (uint32_t)l * (uint32_t)(c * 255 / 63);      /* 0 .. 31*255 */
            const uint32_t rr = (r * k + 31 * 127) / (31 * 255);
            const uint32_t gg = (g * k + 31 * 127) / (31 * 255);
            const uint32_t bb = (b * k + 31 * 127) / (31 * 255);
            const uint16_t v = (uint16_t)(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3));
            lut2[l * 64 + c] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}

/* gauss[i] = 255 exp(-(i/64)^2 / 2): i is the distance in 1/64 sigma. */
static uint8_t s_gauss[256];
static bool s_gauss_ready;

static void hot_tables_init(eyes_t *e)
{
    if (!s_gauss_ready) {
        for (int i = 0; i < 256; i++) {
            const float t = (float)i / 64.f;
            s_gauss[i] = (uint8_t)(255.f * expf(-0.5f * t * t) + 0.5f);
        }
        s_gauss_ready = true;
    }
    for (int g = 0; g < RASTER_G2L_N; g++) {
        const int gg = g > 255 ? 255 : g;
        e->hot_g2l[g] = (uint8_t)(31 - (((255 - gg) * HOT_K) >> 8));
    }
}

/* Fill one eye's per-axis falloff tables for a hot spot at (hx, hy) px with sigma px (Q16). */
static void hot_fill(uint8_t *gx, uint8_t *gy, int32_t hx, int32_t hy, int32_t sigma)
{
    if (sigma < Q16(4.0)) sigma = Q16(4.0);
    const int32_t k = (int32_t)(((int64_t)64 << 32) / sigma);     /* Q16: table index per pixel */
    const int cx = hx >> 16, cy = hy >> 16;
    for (int x = 0; x < BOARD_LCD_H_RES; x++) {
        int d = x - cx; if (d < 0) d = -d;
        const int32_t idx = (int32_t)(((int64_t)d * k) >> 16);
        gx[x] = s_gauss[idx > 255 ? 255 : idx];
    }
    for (int y = 0; y < BOARD_LCD_V_RES; y++) {
        int d = y - cy; if (d < 0) d = -d;
        const int32_t idx = (int32_t)(((int64_t)d * k) >> 16);
        gy[y] = s_gauss[idx > 255 ? 255 : idx];
    }
}

static uint32_t rgb_lerp(uint32_t a, uint32_t b, int32_t w_q16)
{
    if (w_q16 <= 0) return a;
    if (w_q16 > Q16_ONE) w_q16 = Q16_ONE;
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const int32_t ca = (int32_t)((a >> sh) & 0xFF), cb = (int32_t)((b >> sh) & 0xFF);
        out |= (uint32_t)(ca + q16_mul(cb - ca, w_q16)) << sh;
    }
    return out;
}

/* Ease the tints, derive each eye's colour and rebuild its LUTs when it changed (quantised to 8 bits). */
static void update_color(eyes_t *e, uint32_t now_ms)
{
    const uint32_t el = now_ms - e->tint_t0_ms;
    int32_t k = Q16_ONE;
    if (el < e->tint_dur_ms) {
        k = ease_in_out_q16((int32_t)(((uint64_t)el << 16) / e->tint_dur_ms));
    }
    const float f = 1.f / 65536.f;
    for (int i = 0; i < 2; i++) {
        if (k >= Q16_ONE) {
            e->tint_cur[i] = e->tint_to[i];
        } else {
            int32_t *c = (int32_t *)&e->tint_cur[i];
            const int32_t *a = (const int32_t *)&e->tint_from[i], *b = (const int32_t *)&e->tint_to[i];
            for (int j = 0; j < EYE_TINT_FIELDS; j++) {
                c[j] = a[j] + q16_mul(b[j] - a[j], k);
            }
        }
        const eye_tint_t *t = &e->tint_cur[i];
        const float h = e->base_h + (float)(t->hue_shift + e->tint_mod_hue) * f;
        const float sat = e->base_s * (float)t->sat * f * (float)e->mood_sat * f;
        const float lum = e->base_l * ((float)t->lum * f * (float)e->mood_lum * f + (float)e->tint_mod_lum * f);
        uint32_t rgb = hsl_to_rgb(h, sat, lum);
        /* the outgoing pull fades out while the incoming one fades in */
        rgb = rgb_lerp(rgb, e->tint_from[i].pull_rgb, q16_mul(e->tint_from[i].pull, Q16_ONE - k));
        rgb = rgb_lerp(rgb, e->tint_to[i].pull_rgb, q16_mul(e->tint_to[i].pull, k));
        if (rgb != e->rgb[i]) {
            e->rgb[i] = rgb;
            raster_build_lut(e->lut[i], (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
            if (e->hot || e->fx) {
                build_lut2(e->lut2[i], rgb);
            }
        }
    }
}

void eyes_init(eyes_t *e, uint32_t now_ms)
{
    memset(e, 0, sizeof(*e));
    eye_init(&e->eye[0], BOARD_LCD_H_RES / 2 - EYE_SEP_PX, now_ms);
    eye_init(&e->eye[1], BOARD_LCD_H_RES / 2 + EYE_SEP_PX, now_ms);

    e->face_cos = Q16_ONE;
    const eye_tint_t neutral_tint = EYE_TINT_NEUTRAL;
    for (int i = 0; i < 2; i++) {
        e->tint_cur[i] = e->tint_from[i] = e->tint_to[i] = neutral_tint;
        e->rgb[i] = 0xFFFFFFFF;          /* force the first LUT build */
    }
    e->tint_dur_ms = 1;
    e->mood_lum = e->mood_sat = Q16_ONE;
    e->face_scale = Q16_ONE;
    eyes_set_base_color(e, EYE_COLOR);
    hot_tables_init(e);
    update_color(e, now_ms);
    e->idle.rng = esp_random() | 1u;
    e->idle.blink_interval_scale = Q16_ONE;
    e->idle.blink_speed_scale = Q16_ONE;
    e->idle.dart_scale = Q16_ONE;
    e->idle.next_blink_ms = now_ms + rng_range(&e->idle, BLINK_MIN_MS, BLINK_MAX_MS);
    e->idle.next_dart_ms = now_ms + rng_range(&e->idle, DART_MIN_MS, DART_MAX_MS);
    e->idle.dart_t0_ms = now_ms;
    e->idle.dart_dur_ms = 1;
}

void eyes_set_target_ex(eyes_t *e, int eye, const eye_pose_t *pose, uint32_t dur_ms, uint32_t now_ms, eye_ease_t ease)
{
    EyeState *s = &e->eye[eye];
    if (dur_ms == 0) {
        dur_ms = 1;
    }
    for (int i = 0; i < EYE_POSE_FIELDS; i++) {
        s->from.v[i] = s->cur.v[i];
        s->to.v[i] = pose->v[i];
        s->t0_ms[i] = now_ms;
        s->dur_ms[i] = dur_ms;
    }
    s->ease = (uint8_t)ease;
}

void eyes_set_target(eyes_t *e, int eye, const eye_pose_t *pose, uint32_t dur_ms, uint32_t now_ms)
{
    eyes_set_target_ex(e, eye, pose, dur_ms, now_ms, EYE_EASE_SMOOTH);
}

void eyes_clear_mod(eyes_t *e)
{
    memset(e->mod, 0, sizeof(e->mod));
    e->tint_mod_hue = 0;
    e->tint_mod_lum = 0;
    e->face_mod_dx = e->face_mod_dy = e->face_mod_scale = 0;
}

void eyes_set_env(eyes_t *e, int eye, const eye_pose_t *delta)
{
    e->env[eye] = *delta;
}

void eyes_set_face_angle(eyes_t *e, float deg)
{
    e->face_deg = deg;
    if (fabsf(deg) < 0.05f) {
        e->face_rot = false;
        e->face_cos = Q16_ONE;
        e->face_sin = 0;
        return;
    }
    const float rad = deg * 0.01745329f;
    e->face_rot = true;
    e->face_cos = (int32_t)(cosf(rad) * 65536.f);
    e->face_sin = (int32_t)(sinf(rad) * 65536.f);
}

void eyes_set_idle_rates(eyes_t *e, int32_t blink_interval_scale, int32_t blink_speed_scale, int32_t dart_scale)
{
    e->idle.blink_interval_scale = blink_interval_scale;
    e->idle.blink_speed_scale = blink_speed_scale;
    e->idle.dart_scale = dart_scale;
}

void eyes_blink_now(eyes_t *e, uint32_t now_ms)
{
    if (!e->idle.blinking) {
        e->idle.blinking = true;
        e->idle.blink_t0_ms = now_ms;
    }
}

/* Fields that overshoot under EYE_EASE_SNAP: size and position. */
static inline bool field_snaps(int i)
{
    return i <= 1 || i == 6 || i == 7;
}

static void eye_ease(EyeState *s, uint32_t now_ms)
{
    for (int i = 0; i < EYE_POSE_FIELDS; i++) {
        const uint32_t el = now_ms - s->t0_ms[i];
        if (el >= s->dur_ms[i]) {
            s->cur.v[i] = s->to.v[i];
            continue;
        }
        const int32_t t = (int32_t)(((uint64_t)el << 16) / s->dur_ms[i]);
        const int32_t k = (s->ease == EYE_EASE_SNAP && field_snaps(i)) ? ease_out_back_q16(t) : ease_in_out_q16(t);
        s->cur.v[i] = s->from.v[i] + q16_mul(s->to.v[i] - s->from.v[i], k);
    }
}

/* Blink height/width scales (Q16, ONE = open). */
static void idle_blink(eyes_idle_t *idle, uint32_t now_ms, int32_t *bh, int32_t *bw)
{
    *bh = *bw = Q16_ONE;
    if (!idle->blinking) {
        if ((int32_t)(now_ms - idle->next_blink_ms) < 0) {
            return;
        }
        idle->blinking = true;
        idle->blink_t0_ms = now_ms;
    }
    /* elapsed time in table units (the table is stretched by blink_speed_scale) */
    int32_t speed = idle->blink_speed_scale > 1024 ? idle->blink_speed_scale : 1024;
    const uint32_t el = (uint32_t)(((uint64_t)(now_ms - idle->blink_t0_ms) << 16) / (uint32_t)speed);
    if (el >= BLINK_TOTAL_MS) {
        idle->blinking = false;
        uint32_t gap = rng_range(idle, BLINK_MIN_MS, BLINK_MAX_MS);
        gap = (uint32_t)q16_mul((int32_t)gap << 16, idle->blink_interval_scale) >> 16;
        idle->next_blink_ms = now_ms + gap;
        return;
    }
    int i = 1;
    while (i < BLINK_N - 1 && k_blink[i].t_ms <= el) i++;
    const blink_kf_t *a = &k_blink[i - 1], *b = &k_blink[i];
    const int32_t t = q16_div((int32_t)(el - a->t_ms), (int32_t)(b->t_ms - a->t_ms));
    *bh = a->h + q16_mul(b->h - a->h, t);
    *bw = a->w + q16_mul(b->w - a->w, t);
}

static void dart_pos(const eyes_idle_t *idle, uint32_t now_ms, int32_t *ox, int32_t *oy)
{
    const uint32_t el = now_ms - idle->dart_t0_ms;
    int32_t k = Q16_ONE, k2 = Q16_ONE;
    if (el < idle->dart_dur_ms) {
        const int32_t t = q16_div((int32_t)el, (int32_t)idle->dart_dur_ms);
        k = ease_in_out_q16(t);
        int32_t tl = q16_div(t - Q16(0.3), Q16(0.7));
        k2 = ease_in_out_q16(tl);
    }
    const int32_t kx = idle->dart_lag_x ? k2 : k;
    const int32_t ky = idle->dart_lag_x ? k : k2;
    *ox = idle->dart_from_x + q16_mul(idle->dart_to_x - idle->dart_from_x, kx);
    *oy = idle->dart_from_y + q16_mul(idle->dart_to_y - idle->dart_from_y, ky);
}

/* Dart offset (Q16 px) shared by both eyes, plus the current squash amount (Q16). */
static void idle_dart(eyes_idle_t *idle, uint32_t now_ms, int32_t *ox, int32_t *oy, int32_t *squash)
{
    if ((int32_t)(now_ms - idle->next_dart_ms) >= 0) {
        /* start from wherever the eyes are right now */
        dart_pos(idle, now_ms, &idle->dart_from_x, &idle->dart_from_y);
        const int32_t mx = q16_mul(DART_MAX_DX_PX << 16, idle->dart_scale) >> 16;
        const int32_t my = q16_mul(DART_MAX_DY_PX << 16, idle->dart_scale) >> 16;
        idle->dart_to_x = ((int32_t)rng_range(idle, 0, 2 * (uint32_t)mx) - mx) * Q16_ONE;
        idle->dart_to_y = ((int32_t)rng_range(idle, 0, 2 * (uint32_t)my) - my) * Q16_ONE;
        const float ddx = (float)(idle->dart_to_x - idle->dart_from_x) * (1.f / 65536.f);
        const float ddy = (float)(idle->dart_to_y - idle->dart_from_y) * (1.f / 65536.f);
        const float d = sqrtf(ddx * ddx + ddy * ddy);
        uint32_t dur = DART_BASE_MS + (uint32_t)(d * DART_MS_PER_PX);
        if (dur > DART_MAX_DUR_MS) dur = DART_MAX_DUR_MS;
        idle->dart_dur_ms = dur;
        idle->dart_squash = d > DART_LONG_PX ? DART_SQUASH_LONG : (d > DART_MEDIUM_PX ? DART_SQUASH_MED : 0);
        idle->dart_lag_x = ddy > 0.f;
        idle->dart_t0_ms = now_ms;
        idle->next_dart_ms = now_ms + rng_range(idle, DART_MIN_MS, DART_MAX_MS);
        if (d > DART_BLINK_PX && !idle->blinking && rng_range(idle, 0, 99) < DART_BLINK_PCT) {
            idle->blinking = true;
            idle->blink_t0_ms = now_ms;
        }
    }
    dart_pos(idle, now_ms, ox, oy);

    *squash = 0;
    const uint32_t el = now_ms - idle->dart_t0_ms;
    if (idle->dart_squash && el < idle->dart_dur_ms) {
        /* envelope: up quickly to the peak at 35 % of the dart, then back */
        const int32_t t = q16_div((int32_t)el, (int32_t)idle->dart_dur_ms);
        const int32_t env = t < Q16(0.35) ? ease_in_out_q16(q16_div(t, Q16(0.35)))
                                          : ease_in_out_q16(q16_div(Q16_ONE - t, Q16(0.65)));
        *squash = q16_mul(idle->dart_squash, env);
    }
}

static inline int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

typedef struct {
    int32_t dart_x, dart_y;     /* Q16 px, idle darts plus the attention offset */
    int32_t dart_squash;        /* Q16 */
    int32_t bh, bw;             /* blink scales */
    int32_t attend_k;           /* Q16, how much the eyes are attending */
    int32_t fscale;             /* face scale, Q16 */
    int32_t fdx, fdy;           /* face offset, Q16 px */
    uint32_t dt_ms;             /* since the previous frame, 0 = unknown */
} frame_ctx_t;

static void eye_effective_params(int which, EyeState *s, const eye_pose_t *mod, const eye_pose_t *env, const frame_ctx_t *f, EyeParams *p)
{
    eye_pose_t c;
    for (int i = 0; i < EYE_POSE_FIELDS; i++) {
        c.v[i] = s->cur.v[i] + mod->v[i] + env->v[i];
    }
    c.sx = clamp_q16(c.sx, Q16(0.2), Q16(2.0));
    c.sy = clamp_q16(c.sy, Q16(0.02), Q16(2.0));
    c.lid_top = clamp_q16(c.lid_top, 0, Q16_ONE);
    c.lid_bottom = clamp_q16(c.lid_bottom, 0, Q16_ONE);
    /* the lids never cross: a top lid pushed down by a poke stops at the bottom one */
    if (c.lid_top + c.lid_bottom > Q16(0.96)) c.lid_top = Q16(0.96) - c.lid_bottom;
    c.curve = clamp_q16(c.curve, -Q16_ONE, Q16_ONE);
    c.bend = clamp_q16(c.bend, -Q16_ONE, Q16_ONE);
    /* attending: a touch more open */
    c.sy += q16_mul(f->attend_k, Q16(0.04));
    c.lid_top -= q16_mul(f->attend_k, q16_mul(c.lid_top, Q16(0.3)));

    /* face-level placement: the base geometry scaled about the screen centre and shifted */
    const int32_t scx = BOARD_LCD_H_RES / 2 << 16, scy = BOARD_LCD_V_RES / 2 << 16;
    const int32_t bcx = scx + q16_mul(s->base.cx - scx, f->fscale) + f->fdx;
    const int32_t bcy = scy + q16_mul(s->base.cy - scy, f->fscale) + f->fdy;

    /* pose-driven centre (no dart): its velocity drives the stretch */
    const int32_t pcx = bcx + c.dx, pcy = bcy + c.dy;
    int32_t motion_target = 0, ax = Q16(0.5);
    if (f->dt_ms >= 4 && f->dt_ms <= 250) {
        const int32_t vx = (pcx - s->prev_cx) / (int32_t)f->dt_ms;    /* Q16 px/ms */
        const int32_t vy = (pcy - s->prev_cy) / (int32_t)f->dt_ms;
        const int32_t speed = abs32(vx) + abs32(vy);
        if (speed > 0) {
            motion_target = q16_mul(speed, MOTION_GAIN);
            if (motion_target > MOTION_MAX) motion_target = MOTION_MAX;
            ax = q16_div(abs32(vx), speed);
        }
    }
    s->prev_cx = pcx;
    s->prev_cy = pcy;
    /* fast attack, slower release */
    s->motion_k += (motion_target - s->motion_k) * (motion_target > s->motion_k ? 3 : 1) / 4;

    int32_t w = q16_mul(q16_mul(s->base.w, f->fscale), c.sx);
    int32_t h = q16_mul(q16_mul(s->base.h, f->fscale), c.sy);

    /* gaze-dependent size */
    const int32_t gx = c.dx + f->dart_x, gy = c.dy + f->dart_y;
    {
        int32_t k = q16_div(clamp_q16(abs32(gy), 0, LOOK_RANGE_PX << 16), LOOK_RANGE_PX << 16);
        const int32_t sz = gy < 0 ? Q16_ONE + q16_mul(LOOK_UP_GAIN, k) : Q16_ONE - q16_mul(LOOK_DOWN_GAIN, k);
        w = q16_mul(w, sz);
        h = q16_mul(h, sz);
        if ((gx > 0 && which == 1) || (gx < 0 && which == 0)) {
            k = q16_div(clamp_q16(abs32(gx), 0, LOOK_RANGE_PX << 16), LOOK_RANGE_PX << 16);
            h = q16_mul(h, Q16_ONE + q16_mul(LOOK_SIDE_GAIN, k));
        }
    }
    /* squash on darts, stretch along fast moves */
    if (f->dart_squash) {
        h = q16_mul(h, Q16_ONE - f->dart_squash);
        w = q16_mul(w, Q16_ONE + f->dart_squash / 2);
    }
    if (s->motion_k > 64) {
        const int32_t ay = Q16_ONE - ax;
        w = q16_mul(w, Q16_ONE + q16_mul(s->motion_k, ax - q16_mul(MOTION_PERP, ay)));
        h = q16_mul(h, Q16_ONE + q16_mul(s->motion_k, ay - q16_mul(MOTION_PERP, ax)));
    }

    /* blink: the eye thins and widens; the lids fade out and the sliver sits where the visible part was */
    int32_t cy = bcy + c.dy + f->dart_y;
    if (f->bh < Q16_ONE) {
        const int32_t band_c = cy + q16_mul(h, c.lid_top - c.lid_bottom) / 2;
        cy += q16_mul(Q16_ONE - f->bh, band_c - cy);
        c.lid_top = q16_mul(c.lid_top, f->bh);
        c.lid_bottom = q16_mul(c.lid_bottom, f->bh);
        c.curve = q16_mul(c.curve, f->bh);
        c.bend = q16_mul(c.bend, f->bh);
        h = q16_mul(h, f->bh);
        w = q16_mul(w, f->bw);
    }
    if (h < (EYE_MIN_H_PX << 16)) {
        h = EYE_MIN_H_PX << 16;
    }

    p->cx = bcx + c.dx + f->dart_x;
    p->cy = cy;
    p->w = w;
    p->h = h;
    /* corner radii follow the smaller of the effective scales, so a squashed eye keeps its capsule ends */
    const int32_t esx = q16_div(w, s->base.w), esy = q16_div(h, s->base.h);
    const int32_t es = esx < esy ? esx : esy;
    for (int i = 0; i < 4; i++) {
        p->rad[i] = q16_mul(q16_mul(s->base.rad[i], es), c.rad[i] < 0 ? 0 : c.rad[i]);
        p->rady[i] = q16_mul(q16_mul(s->base.rady[i], es), c.rady[i] < 0 ? 0 : c.rady[i]);
    }
    p->lid_top = c.lid_top;
    p->lid_bottom = c.lid_bottom;
    p->slant = c.slant;
    p->slant_b = c.slant_b;
    p->curve = q16_mul(h, c.curve);
    p->bend = q16_mul(h, c.bend);
    p->angle = c.angle;
    /* hot spot: ahead of the gaze, kept inside the eye */
    p->hot_x = p->cx + clamp_q16(q16_mul(gx, HOT_GAZE_GAIN), -w / 4, w / 4);
    p->hot_y = p->cy + clamp_q16(q16_mul(gy, HOT_GAZE_GAIN), -h / 4, h / 4);
}

static void rotate_about_centre(const eyes_t *e, int32_t x, int32_t y, int32_t *ox, int32_t *oy)
{
    const int32_t rx = x - (BOARD_LCD_H_RES / 2 << 16), ry = y - (BOARD_LCD_V_RES / 2 << 16);
    *ox = (BOARD_LCD_H_RES / 2 << 16) + (int32_t)(((int64_t)rx * e->face_cos - (int64_t)ry * e->face_sin) >> 16);
    *oy = (BOARD_LCD_V_RES / 2 << 16) + (int32_t)(((int64_t)rx * e->face_sin + (int64_t)ry * e->face_cos) >> 16);
}

static void params_to_shape(eyes_t *e, int which, const EyeParams *p, raster_shape_t *s)
{
    int32_t hx = p->hot_x, hy = p->hot_y;
    if (e->face_rot) {
        /* the eye's centre swings around the screen centre with the face */
        rotate_about_centre(e, p->cx, p->cy, &s->cx, &s->cy);
        rotate_about_centre(e, hx, hy, &hx, &hy);
    } else {
        s->cx = p->cx;
        s->cy = p->cy;
    }
    /* the eye's own angle composes with the face angle */
    if (p->angle > -Q16(0.05) && p->angle < Q16(0.05)) {
        s->rot = e->face_rot;
        s->rc = e->face_cos;
        s->rs = e->face_sin;
    } else {
        const float rad = (e->face_deg + (float)p->angle * (1.f / 65536.f)) * 0.01745329f;
        s->rot = true;
        s->rc = (int32_t)(cosf(rad) * 65536.f);
        s->rs = (int32_t)(sinf(rad) * 65536.f);
    }
    s->hw = p->w / 2;
    s->hh = p->h / 2;
    for (int i = 0; i < 4; i++) {
        s->rad[i] = p->rad[i];
        s->rady[i] = p->rady[i];
    }
    s->top_base = (s->cy - s->hh) + q16_mul(p->h, p->lid_top);
    s->slant = p->slant;
    s->bend = p->bend;
    s->bot_base = (s->cy + s->hh) - q16_mul(p->h, p->lid_bottom);
    s->bot_slant = p->slant_b;
    s->curve = p->curve;
    s->lut = e->lut[which];
    s->hot = e->hot && !e->fx;
    s->fx = e->fx;
    if (e->fx) {
        s->fx_mix = (int)(e->fx_mix * 256.f);
        s->bar_lit = 31;
        s->bar_dim = 4;
        s->lut2 = e->lut2[which];
        /* bars: eight across the eye in its own frame, rising from its bottom edge (local +hh) */
        s->bar_w = s->hw / 4;
        for (int i = 0; i < 8; i++) {
            const float hf = e->bar_h[which][i];
            s->bar_top[i] = hf <= 0.f ? s->hh + Q16_ONE : s->hh - (int32_t)(hf * (float)(2 * s->hh));
        }
        /* disco: facets a tenth of the eye's height, the turn in eye widths */
        s->disco_facet = s->hh / 5;
        s->disco_spin = (int32_t)(e->disco_spin * (float)(2 * s->hw));
        s->disco_seed = e->disco_seed;
        /* spots */
        s->spots_n = e->spots_n;
        for (int i = 0; i < s->spots_n; i++) {
            s->spot_x[i] = (int32_t)(e->spot_x[i] * (float)s->hw);
            s->spot_y[i] = (int32_t)(e->spot_y[i] * (float)s->hh);
        }
        s->spot_r = (int32_t)(e->spot_r * (float)s->hh);
    }
    if (s->hot) {
        hot_fill(e->hot_gx[which], e->hot_gy[which], hx, hy, q16_mul(p->h, HOT_SIGMA));
        s->hot_gx = e->hot_gx[which];
        s->hot_gy = e->hot_gy[which];
        s->hot_g2l = e->hot_g2l;
        s->lut2 = e->lut2[which];
    }
    raster_shape_finalize(s, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
}

void eyes_update(eyes_t *e, uint32_t now_ms, raster_shape_t out[2])
{
    frame_ctx_t f;
    idle_blink(&e->idle, now_ms, &f.bh, &f.bw);
    /* attention eases in and out; while attending the darts shrink to about a pixel */
    e->attend_k += ((e->attend ? Q16_ONE : 0) - e->attend_k) / 6;
    e->attend_x += (e->attend_tx - e->attend_x) / 5;
    e->attend_y += (e->attend_ty - e->attend_y) / 5;
    const int32_t saved_dart_scale = e->idle.dart_scale;
    e->idle.dart_scale = saved_dart_scale + q16_mul(e->attend_k, ATTEND_DART - saved_dart_scale);
    idle_dart(&e->idle, now_ms, &f.dart_x, &f.dart_y, &f.dart_squash);
    e->idle.dart_scale = saved_dart_scale;
    f.dart_x += e->attend_x;
    f.dart_y += e->attend_y;
    f.attend_k = e->attend_k;
    f.fscale = clamp_q16(e->face_scale + e->face_mod_scale, Q16(0.3), Q16(1.6));
    f.fdx = e->face_dx + e->face_mod_dx;
    f.fdy = e->face_dy + e->face_mod_dy;
    f.dt_ms = e->have_prev ? now_ms - e->prev_ms : 0;
    e->prev_ms = now_ms;
    e->have_prev = true;

    EyeParams p[2];
    for (int i = 0; i < 2; i++) {
        EyeState *s = &e->eye[i];
        eye_ease(s, now_ms);
        eye_effective_params(i, s, &e->mod[i], &e->env[i], &f, &p[i]);
    }
    /* closing eyes line up on a common height */
    if (f.bh < Q16_ONE) {
        const int32_t mid = (p[0].cy + p[1].cy) / 2;
        for (int i = 0; i < 2; i++) {
            p[i].cy += q16_mul(Q16_ONE - f.bh, mid - p[i].cy);
        }
    }
    update_color(e, now_ms);
    for (int i = 0; i < 2; i++) {
        params_to_shape(e, i, &p[i], &out[i]);
    }
}
