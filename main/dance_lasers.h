#pragma once
#include "raster.h"
/* Four analytic beam quads. Snapshot is immutable while the two cores paint. */
typedef struct {
    bool active;
    uint32_t bucket;
    float aim[4];
    int damage[4];            /* old + new beam bounds: x0,y0,x1,y1 */
    raster_shape_t beam[4];
    uint16_t lut[4][256];
} dance_lasers_t;
/* True when the background changed (including switch-off): repaint damage. */
bool dance_lasers_update(dance_lasers_t *l,float mix,uint32_t beat,uint32_t now_ms,float face_deg);
void dance_lasers_paint(const dance_lasers_t *l,uint16_t *dst,int x,int y,int w,int rows,const raster_shape_t *eyes);
