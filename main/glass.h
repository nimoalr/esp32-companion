#pragma once
#include "gfx.h"
#define GLASS_SHARDS 9
#define GLASS_MAX_DIRTY 128
#define GLASS_POINTS 12
typedef struct { int xy[GLASS_POINTS][2],branch[2][2]; bool visible,moving; } glass_piece_t;
typedef struct {
    glass_piece_t piece[GLASS_SHARDS];
    int stage,hole;
    unsigned released;
    uint32_t bucket;
    uint16_t dirty[15];
} glass_t;
typedef struct {int x0,y0,x1,y1;} glass_rect_t;
/* Cached 30 Hz geometry and sparse old/new edge damage; no per-pixel physics. */
int glass_update(glass_t *g,int stage,uint32_t age,uint32_t now,glass_rect_t out[GLASS_MAX_DIRTY]);
void glass_paint(const glass_t *g,const gfx_band_t *b);
