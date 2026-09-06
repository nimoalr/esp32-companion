#include "adpcm.h"

static const int16_t k_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t k_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static inline int16_t step_one(adpcm_state_t *st, uint8_t code)
{
    const int step = k_step[st->index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    int p = st->predictor + ((code & 8) ? -diff : diff);
    if (p > 32767) p = 32767;
    if (p < -32768) p = -32768;
    st->predictor = (int16_t)p;
    int idx = st->index + k_index[code];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    st->index = (int8_t)idx;
    return st->predictor;
}

void adpcm_decode(adpcm_state_t *st, const uint8_t *in, int16_t *out, int n)
{
    for (int i = 0; i < n; i += 2) {
        const uint8_t b = in[i >> 1];
        out[i] = step_one(st, b & 15);
        out[i + 1] = step_one(st, b >> 4);
    }
}

static inline uint8_t encode_one(adpcm_state_t *st, int16_t x)
{
    const int step = k_step[st->index];
    int diff = x - st->predictor;
    uint8_t code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= step) { code |= 4; diff -= step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; }
    if (diff >= step >> 2) code |= 1;
    step_one(st, code);
    return code;
}

void adpcm_encode(adpcm_state_t *st, const int16_t *in, uint8_t *out, int n)
{
    for (int i = 0; i < n; i += 2) {
        const uint8_t lo = encode_one(st, in[i]), hi = encode_one(st, in[i + 1]);
        out[i >> 1] = (uint8_t)(lo | hi << 4);
    }
}
