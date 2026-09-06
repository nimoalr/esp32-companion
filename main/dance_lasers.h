#pragma once
#include "raster.h"
#include "audio_features.h"

#define DANCE_LASER_MAX 24
#define DANCE_LASER_EMITTERS 8
/* Compact antialiased rays; no general polygon storage per beam. */
typedef struct {
    int major0, major1;
    int origin_x;              /* fixtures share screen y=233, spread across x */
    int32_t minor0, step;     /* Q16 minor coordinate and increment per major pixel */
    int bounds[4];
    bool steep;
    uint8_t palette, light;
} dance_ray_t;
/* Snapshot is immutable while the two cores paint. */
typedef struct {
    bool active, palette_ready;
    uint32_t bucket, updated_ms;
    float aim[DANCE_LASER_MAX], fade[DANCE_LASER_MAX];
    int damage[4];            /* old + new beam bounds: x0,y0,x1,y1 */
    dance_ray_t beam[DANCE_LASER_MAX];
    uint16_t lut[4][256];     /* four shared intensity palettes */
} dance_lasers_t;
/* True when the background changed (including switch-off): repaint damage. */
bool dance_lasers_update(dance_lasers_t *l, float mix, const audio_features_t *audio,
                         uint32_t now_ms, float face_deg);
void dance_lasers_paint(const dance_lasers_t *l, uint16_t *dst, int x, int y,
                        int w, int rows, const raster_shape_t *eyes);
