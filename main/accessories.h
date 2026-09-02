/*
 * Props drawn around the eyes: headphones when music is on, stars and X eyes
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

#define ACC_MAX_DIRTY 6

typedef struct {
    /* headphones: slide in from above */
    bool head_on;
    int32_t head_y;         /* Q16 px offset, 0 = in place, negative = above the screen */
    uint32_t head_t0_ms;
    /* knocked out */
    bool ko_on;
    uint32_t ko_t0_ms;
    int ko_prev[3][2];
    /* sleeping z's */
    bool zz_on;
    uint32_t zz_t0_ms;
    int zz_prev_y;
    int eye_cx[2], eye_cy;  /* where the X eyes go (upright face) */
    float angle_deg;        /* face rotation, clockwise */
    float prev_angle_deg;
    bool first;
} accessories_t;

void acc_init(accessories_t *a, int left_cx, int right_cx, int cy);
void acc_set_headphones(accessories_t *a, bool on, uint32_t now_ms);
void acc_set_knocked_out(accessories_t *a, bool on, uint32_t now_ms);
void acc_set_zz(accessories_t *a, bool on, uint32_t now_ms);
void acc_set_angle(accessories_t *a, float deg);

/* Advance animations; returns the rectangles that changed since the last call. */
int acc_update(accessories_t *a, uint32_t now_ms, acc_rect_t out[ACC_MAX_DIRTY]);
/* Paint everything visible, clipped to the band, over the eyes. */
void acc_paint(const accessories_t *a, const gfx_band_t *band, uint32_t now_ms);
bool acc_any(const accessories_t *a);
