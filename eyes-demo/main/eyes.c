#include "eyes.h"

#include <string.h>
#include "esp_random.h"
#include "board.h"

/* Resting geometry: two orange capsules either side of the centre of the 466 px disc. */
#define EYE_W_PX        110
#define EYE_H_PX        140
#define EYE_R_PX        48
#define EYE_SEP_PX      95          /* centre offset from the screen centre */
#define EYE_CY_PX       (BOARD_LCD_V_RES / 2)
#define EYE_COLOR       0xFF8C00    /* orange */
#define EYE_MIN_H_PX    4           /* a closed eye is still a thin sliver */

/* Blink: random 2..6 s apart, ~60 ms close + ~60 ms open. */
#define BLINK_MIN_MS    2000
#define BLINK_MAX_MS    6000
#define BLINK_CLOSE_MS  60
#define BLINK_OPEN_MS   60

/* Saccade: every 1..3 s, a few pixels, ~90 ms. */
#define SACC_MIN_MS     1000
#define SACC_MAX_MS     3000
#define SACC_DUR_MS     90
#define SACC_MAX_DX_PX  6
#define SACC_MAX_DY_PX  4

static inline int32_t q16_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 16);
}

int32_t ease_in_out_q16(int32_t t)
{
    if (t <= 0) return 0;
    if (t >= Q16_ONE) return Q16_ONE;
    const int64_t tt = ((int64_t)t * t) >> 16;                  /* t^2 */
    return (int32_t)((tt * ((3 << 16) - 2 * (int64_t)t)) >> 16); /* t^2 (3 - 2t) */
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
    s->base.radius = EYE_R_PX << 16;
    s->base.color = EYE_COLOR;
    const eye_pose_t neutral = EYE_POSE_NEUTRAL;
    s->cur = s->from = s->to = neutral;
    for (int i = 0; i < EYE_POSE_FIELDS; i++) {
        s->t0_ms[i] = now_ms;
        s->dur_ms[i] = 1;
    }
    raster_build_lut(s->lut, (EYE_COLOR >> 16) & 0xFF, (EYE_COLOR >> 8) & 0xFF, EYE_COLOR & 0xFF);
}

void eyes_init(eyes_t *e, uint32_t now_ms)
{
    memset(e, 0, sizeof(*e));
    eye_init(&e->eye[0], BOARD_LCD_H_RES / 2 - EYE_SEP_PX, now_ms);
    eye_init(&e->eye[1], BOARD_LCD_H_RES / 2 + EYE_SEP_PX, now_ms);

    e->idle.rng = esp_random() | 1u;
    e->idle.blink_interval_scale = Q16_ONE;
    e->idle.blink_speed_scale = Q16_ONE;
    e->idle.next_blink_ms = now_ms + rng_range(&e->idle, BLINK_MIN_MS, BLINK_MAX_MS);
    e->idle.next_sacc_ms = now_ms + rng_range(&e->idle, SACC_MIN_MS, SACC_MAX_MS);
    e->idle.sacc_t0_ms = now_ms;
}

void eyes_set_target(eyes_t *e, int eye, const eye_pose_t *pose, uint32_t dur_ms, uint32_t now_ms)
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
}

void eyes_set_idle_rates(eyes_t *e, int32_t blink_interval_scale, int32_t blink_speed_scale)
{
    e->idle.blink_interval_scale = blink_interval_scale;
    e->idle.blink_speed_scale = blink_speed_scale;
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
        const int32_t k = ease_in_out_q16(t);
        s->cur.v[i] = s->from.v[i] + q16_mul(s->to.v[i] - s->from.v[i], k);
    }
}

/* Blink amount in Q16: 0 open .. ONE closed. */
static int32_t idle_blink(eyes_idle_t *idle, uint32_t now_ms)
{
    const uint32_t close_ms = (uint32_t)q16_mul(BLINK_CLOSE_MS << 16, idle->blink_speed_scale) >> 16;
    const uint32_t open_ms = (uint32_t)q16_mul(BLINK_OPEN_MS << 16, idle->blink_speed_scale) >> 16;

    if (!idle->blinking) {
        if ((int32_t)(now_ms - idle->next_blink_ms) < 0) {
            return 0;
        }
        idle->blinking = true;
        idle->blink_t0_ms = now_ms;
    }
    const uint32_t el = now_ms - idle->blink_t0_ms;
    if (el < close_ms) {
        return ease_in_out_q16((int32_t)((uint64_t)el * Q16_ONE / close_ms));
    }
    if (el < close_ms + open_ms) {
        return Q16_ONE - ease_in_out_q16((int32_t)((uint64_t)(el - close_ms) * Q16_ONE / open_ms));
    }
    idle->blinking = false;
    uint32_t gap = rng_range(idle, BLINK_MIN_MS, BLINK_MAX_MS);
    gap = (uint32_t)q16_mul((int32_t)gap << 16, idle->blink_interval_scale) >> 16;
    idle->next_blink_ms = now_ms + gap;
    return 0;
}

/* Saccade offset (Q16 px) shared by both eyes. */
static void idle_saccade(eyes_idle_t *idle, uint32_t now_ms, int32_t *ox, int32_t *oy)
{
    if ((int32_t)(now_ms - idle->next_sacc_ms) >= 0) {
        idle->sacc_from_x = idle->sacc_to_x;
        idle->sacc_from_y = idle->sacc_to_y;
        idle->sacc_to_x = ((int32_t)rng_range(idle, 0, 2 * SACC_MAX_DX_PX) - SACC_MAX_DX_PX) << 16;
        idle->sacc_to_y = ((int32_t)rng_range(idle, 0, 2 * SACC_MAX_DY_PX) - SACC_MAX_DY_PX) << 16;
        idle->sacc_t0_ms = now_ms;
        idle->next_sacc_ms = now_ms + rng_range(idle, SACC_MIN_MS, SACC_MAX_MS);
    }
    const uint32_t el = now_ms - idle->sacc_t0_ms;
    int32_t k = Q16_ONE;
    if (el < SACC_DUR_MS) {
        k = ease_in_out_q16((int32_t)((uint64_t)el * Q16_ONE / SACC_DUR_MS));
    }
    *ox = idle->sacc_from_x + q16_mul(idle->sacc_to_x - idle->sacc_from_x, k);
    *oy = idle->sacc_from_y + q16_mul(idle->sacc_to_y - idle->sacc_from_y, k);
}

static void eye_effective_params(const EyeState *s, int32_t blink, int32_t sx_off, int32_t sy_off, EyeParams *p)
{
    const eye_pose_t *c = &s->cur;
    p->color = s->base.color;
    p->cx = s->base.cx + c->dx + sx_off;
    p->cy = s->base.cy + c->dy + sy_off;
    p->w = q16_mul(s->base.w, c->sx);
    int32_t h = q16_mul(s->base.h, c->sy);
    h = q16_mul(h, Q16_ONE - blink);
    if (h < (EYE_MIN_H_PX << 16)) {
        h = EYE_MIN_H_PX << 16;
    }
    p->h = h;
    int32_t r = q16_mul(s->base.radius, c->sx < c->sy ? c->sx : c->sy);
    if (r > h / 2) r = h / 2;
    if (r > p->w / 2) r = p->w / 2;
    p->radius = r;
    p->lid_top = c->lid_top;
    p->lid_bottom = c->lid_bottom;
    p->slant = c->slant;
    p->curve = q16_mul(h, c->curve);
}

static void params_to_shape(const EyeParams *p, const uint16_t *lut, raster_shape_t *s)
{
    s->cx = p->cx;
    s->cy = p->cy;
    s->hw = p->w / 2;
    s->hh = p->h / 2;
    s->r = p->radius;
    s->top_base = (p->cy - s->hh) + q16_mul(p->h, p->lid_top);
    s->slant = p->slant;
    s->bot_base = (p->cy + s->hh) - q16_mul(p->h, p->lid_bottom);
    s->curve = p->curve;
    s->lut = lut;
    raster_shape_finalize(s, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
}

void eyes_update(eyes_t *e, uint32_t now_ms, raster_shape_t out[2])
{
    const int32_t blink = idle_blink(&e->idle, now_ms);
    int32_t sx, sy;
    idle_saccade(&e->idle, now_ms, &sx, &sy);

    for (int i = 0; i < 2; i++) {
        EyeState *s = &e->eye[i];
        eye_ease(s, now_ms);
        EyeParams p;
        eye_effective_params(s, blink, sx, sy, &p);
        params_to_shape(&p, s->lut, &out[i]);
    }
}
