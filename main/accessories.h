/*
 * Props drawn around the eyes: orbiting stars
 * when knocked out, floating z's when asleep. Pure C on gfx.h; the render
 * task composes them into the same bands as the eyes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

typedef struct {
    int x0, y0, x1, y1;
} acc_rect_t;

#define ACC_MAX_DIRTY 64        /* a charge-ring redraw is one thin rect per 16-row band */

typedef struct {
    /* knocked out */
    bool ko_on;
    uint32_t ko_t0_ms;
    int ko_prev[3][2];
    /* sleeping z's */
    bool zz_on;
    uint32_t zz_t0_ms;
    int zz_prev_y;
    /* charge gauge on the rim while USB power is present: track and arc sweep in and out */
    bool chg_on;            /* USB present: the target state */
    bool chg_charging, chg_dirty;
    int chg_pct;
    float chg_track, chg_arc;       /* drawn extents in degrees from the start of the sweep */
    float chg_prev_track, chg_prev_arc;
    uint32_t chg_ms;
    int eye_cx[2], eye_cy;  /* face anchors in upright coordinates */
    float angle_deg;        /* face rotation, clockwise */
    float prev_angle_deg;
    bool first;
} accessories_t;

void acc_init(accessories_t *a, int left_cx, int right_cx, int cy);
void acc_set_knocked_out(accessories_t *a, bool on, uint32_t now_ms);
void acc_set_zz(accessories_t *a, bool on, uint32_t now_ms);
void acc_set_angle(accessories_t *a, float deg);
/* Rim gauge: shown while `on`; the arc length is the battery percentage, green while charging. */
void acc_set_charge(accessories_t *a, bool on, int pct, bool charging);
/* The screen was cleared behind our back: repaint the static parts next frame. */
void acc_redraw(accessories_t *a);

/* Advance animations; returns the rectangles that changed since the last call. */
int acc_update(accessories_t *a, uint32_t now_ms, acc_rect_t out[ACC_MAX_DIRTY]);
/* Paint everything visible, clipped to the band, over the eyes. */
void acc_paint(const accessories_t *a, const gfx_band_t *band, uint32_t now_ms);
bool acc_any(const accessories_t *a);
