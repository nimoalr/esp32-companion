#pragma once
#include "eyes.h"
#include "gfx.h"
typedef struct { bool down; float x,y,travel,span_x,span_y; uint32_t since,last; unsigned count; } petting_t;
/* Coordinates are in the upright face frame; emits during continuous caresses. */
bool petting_update(petting_t *p, bool down, float x, float y, uint32_t now);
typedef struct { float x[2],y[2],vx[2],vy[2]; uint32_t ms,hit_ms; unsigned hits; } pucks_t;
void pucks_init(pucks_t *p, uint32_t now, uint32_t seed);
/* Fixed bounded substeps; circles collide with each other and the display rim. */
float pucks_update(pucks_t *p, uint32_t now, float gx, float gy);
/* Static glass fractures; stage 4 opens a jagged hole. No per-frame allocation. */
void cracks_paint(const gfx_band_t *b, int stage);
