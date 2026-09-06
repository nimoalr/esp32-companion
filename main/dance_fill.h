#pragma once
#include <stdint.h>
#define DANCE_TEX_N 64
#define DANCE_FILL_DIM 4    /* same dim eye silhouette under every dance fill (0..31) */
/* One small procedural lightness image, shared by both eyes for this frame. */
typedef struct { uint8_t tex[DANCE_TEX_N*DANCE_TEX_N]; } dance_fill_t;
void dance_fill_init(void);
void dance_fill_disco(dance_fill_t *f, float turns);
void dance_fill_spots(dance_fill_t *f, int n, const float *x, const float *y, float width);
