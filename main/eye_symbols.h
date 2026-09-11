#pragma once
#include "raster.h"

typedef enum {
    EYE_SYMBOL_NONE = 0, EYE_SYMBOL_HEART, EYE_SYMBOL_BROKEN,
    EYE_SYMBOL_SEVEN, EYE_SYMBOL_DIAMOND, EYE_SYMBOL_REEL,
} eye_symbol_t;

void eye_symbol_shape(raster_shape_t *s, eye_symbol_t symbol, int32_t split_q16, int32_t reel_q16);
