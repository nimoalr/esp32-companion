#pragma once
#include "eyes.h"
#include "gfx.h"
typedef struct { bool down,latched; float x,y,travel,span_x,span_y,dir_x,dir_y,peak_x,peak_y; uint32_t since,last; unsigned count; } petting_t;
/* Coordinates are in the upright face frame; emits during continuous caresses. */
bool petting_update(petting_t *p, bool down, float x, float y, uint32_t now);
typedef struct { float x[2],y[2],vx[2],vy[2],hw[2],hh[2],radius[2]; uint32_t ms,hit_ms; unsigned hits; } pucks_t;
void pucks_init(pucks_t *p, uint32_t now, uint32_t seed);
/* Bounded substeps; rounded eye bodies collide with each other and the circular rim.
 * gx/gy point toward the low side (negative calibrated specific force), in g. */
float pucks_update(pucks_t *p, uint32_t now, float gx, float gy);
/* Static glass fractures; stage 4 opens a jagged hole. No per-frame allocation. */
void cracks_paint(const gfx_band_t *b, int stage);

#define CRACK_RAYS 9
typedef struct {int xy[5][2],branch[2];} crack_path_t;
void cracks_paths(crack_path_t out[CRACK_RAYS]);
void cracks_line(const gfx_band_t *b,int x,int y,int xx,int yy,uint16_t color);
bool cracks_in_hole(int x,int y);
void cracks_hole(const gfx_band_t *b,int scale);
