/* IMA ADPCM, 4 bits per sample: the word clips' format. Pure C, shared with the host encoder. */
#pragma once

#include <stdint.h>

typedef struct {
    int16_t predictor;
    int8_t index;
} adpcm_state_t;

/* Decode n samples (n/2 bytes; n even) into out. */
void adpcm_decode(adpcm_state_t *st, const uint8_t *in, int16_t *out, int n);
/* Encode n samples (n even) into n/2 bytes. */
void adpcm_encode(adpcm_state_t *st, const int16_t *in, uint8_t *out, int n);
