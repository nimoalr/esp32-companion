/*
 * Channel vocoder for the word clips: a spoken WAV (16 kHz mono 16-bit, from a host
 * text-to-speech) becomes the character's voice. The speech is split into 24 bands
 * whose envelopes drive the same bands of a synthetic carrier: a sawtooth that follows
 * the speech's own pitch (autocorrelation) shifted up, or noise where the speech is
 * unvoiced. A little of the speech above 3 kHz is added back for the consonants.
 *
 *   vocode in.wav out.wav [base_hz=587] [expand=2.0] [robot=0.85]
 *
 * The carrier is the procedural voice's own tone (main/voice.c: five harmonics, an
 * octave partner, 5.5 Hz vibrato, a 2.8 kHz one-pole) so the words match the chirps.
 * Its pitch sits at base_hz (587 = the high register) and follows the speech's
 * intonation measured in semitones around the speech's median pitch, multiplied by
 * expand, because text-to-speech is flat. robot is the share of vocoded signal against
 * the dry speech (1 = pure vocoder).
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS 16000
#define BANDS 24
#define F_LO 180.f
#define F_HI 7000.f

typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } biq_t;

static void bp_init(biq_t *q, float f, float Q)
{
    const float w = 2.f * (float)M_PI * f / FS, sn = sinf(w), cs = cosf(w), al = sn / (2.f * Q), a0 = 1.f + al;
    q->b0 = al / a0; q->b1 = 0.f; q->b2 = -al / a0; q->a1 = -2.f * cs / a0; q->a2 = (1.f - al) / a0;
    q->x1 = q->x2 = q->y1 = q->y2 = 0.f;
}

static inline float biq(biq_t *q, float x)
{
    const float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}

static int16_t *wav_read(const char *path, int *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fprintf(stderr, "%s: not a WAV\n", path); exit(1); }
    int16_t *pcm = NULL;
    *n = 0;
    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        const uint32_t len = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            fread(fmt, 1, 16, f);
            const int chans = fmt[2] | fmt[3] << 8, rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16, bits = fmt[14] | fmt[15] << 8;
            if (chans != 1 || rate != FS || bits != 16) { fprintf(stderr, "%s: need 16 kHz mono 16-bit, got %d ch %d Hz %d bit\n", path, chans, rate, bits); exit(1); }
            fseek(f, (long)len - 16, SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            pcm = malloc(len);
            *n = (int)(fread(pcm, 1, len, f) / 2);
            break;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
    fclose(f);
    if (!pcm) { fprintf(stderr, "%s: no data\n", path); exit(1); }
    return pcm;
}

static void wav_write(const char *path, const int16_t *pcm, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t data = (uint32_t)n * 2, rate = FS, brate = rate * 2, fmtlen = 16, riff = 36 + data;
    const uint16_t ch = 1, bits = 16, align = 2, fmt = 1;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtlen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm, 2, (size_t)n, f);
    fclose(f);
}

/* pitch track: autocorrelation over 30 ms every 10 ms, 80..400 Hz; 0 = unvoiced */
static void pitch_track(const float *x, int n, float *hz_out, int frames)
{
    const int win = FS * 30 / 1000, hop = FS * 10 / 1000, lmin = FS / 400, lmax = FS / 80;
    for (int f = 0; f < frames; f++) {
        const int s = f * hop;
        float e0 = 0.f;
        for (int i = 0; i < win && s + i < n; i++) e0 += x[s + i] * x[s + i];
        float best = 0.f;
        int bl = 0;
        for (int l = lmin; l <= lmax; l++) {
            float acc = 0.f, el = 0.f;
            for (int i = 0; i < win && s + i + l < n; i++) { acc += x[s + i] * x[s + i + l]; el += x[s + i + l] * x[s + i + l]; }
            const float r = acc / (sqrtf(e0 * el) + 1e-9f);
            if (r > best) { best = r; bl = l; }
        }
        hz_out[f] = (best > 0.55f && e0 > 1e-4f * win) ? (float)FS / (float)bl : 0.f;
    }
    /* fill short unvoiced holes from the neighbours so the carrier does not stutter */
    for (int f = 1; f + 1 < frames; f++) if (hz_out[f] == 0.f && hz_out[f - 1] > 0.f && hz_out[f + 1] > 0.f) hz_out[f] = 0.5f * (hz_out[f - 1] + hz_out[f + 1]);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: vocode in.wav out.wav [pitch_mult] [robot]\n"); return 1; }
    const float base_hz = argc > 3 ? (float)atof(argv[3]) : 587.f;
    const float expand = argc > 4 ? (float)atof(argv[4]) : 2.f;
    const float robot = argc > 5 ? (float)atof(argv[5]) : 0.85f;
    int n;
    int16_t *pcm = wav_read(argv[1], &n);
    float *x = malloc(sizeof(float) * (size_t)n), *y = calloc((size_t)n, sizeof(float));
    for (int i = 0; i < n; i++) x[i] = pcm[i] / 32768.f;

    const int hop = FS * 10 / 1000, frames = n / hop + 1;
    float *hz = calloc((size_t)frames, sizeof(float));
    pitch_track(x, n, hz, frames);
    /* the speech's median voiced pitch: intonation is measured around it */
    float voiced_list[4096];
    int nv = 0;
    for (int f = 0; f < frames && nv < 4096; f++) if (hz[f] > 0.f) voiced_list[nv++] = hz[f];
    for (int a = 1; a < nv; a++) { const float t = voiced_list[a]; int b = a; while (b > 0 && voiced_list[b - 1] > t) { voiced_list[b] = voiced_list[b - 1]; b--; } voiced_list[b] = t; }
    const float median_hz = nv ? voiced_list[nv / 2] : 200.f;

    biq_t an[BANDS][2], sy[BANDS][2];
    float env[BANDS] = { 0 };
    for (int b = 0; b < BANDS; b++) {
        const float fc = F_LO * powf(F_HI / F_LO, (float)b / (BANDS - 1));
        const float Q = 6.f;
        for (int k = 0; k < 2; k++) { bp_init(&an[b][k], fc, Q); bp_init(&sy[b][k], fc, Q); }
    }
    biq_t hp1, hp2;     /* sibilance path: the speech above ~3 kHz */
    bp_init(&hp1, 4500.f, 0.7f); bp_init(&hp2, 4500.f, 0.7f);
    const float env_k_up = 1.f - expf(-2.f * (float)M_PI * 60.f / FS), env_k_dn = 1.f - expf(-2.f * (float)M_PI * 25.f / FS);
    float ph = 0.f, cur_st = 0.f, vib = 0.f;
    unsigned seed = 1;
    float lp_car = 0.f;
    const float lp_k = 1.f - expf(-2.f * (float)M_PI * 2800.f / FS);
    for (int i = 0; i < n; i++) {
        /* carrier: pitch from the track (interpolated), shifted; noise where unvoiced */
        const int f = i / hop;
        const float t = (float)(i - f * hop) / hop;
        const float h0 = hz[f], h1 = f + 1 < frames ? hz[f + 1] : h0;
        const float voiced_hz = h0 > 0.f && h1 > 0.f ? h0 + (h1 - h0) * t : (h0 > 0.f ? h0 : h1);
        const bool voiced = voiced_hz > 0.f;
        if (voiced) {
            /* intonation in semitones around the median, expanded, clamped to the register's range */
            float st = expand * 12.f * log2f(voiced_hz / median_hz);
            if (st > 12.f) st = 12.f;
            if (st < -4.f) st = -4.f;
            cur_st += (st - cur_st) * 0.03f;
        }
        vib += 5.5f / FS;
        if (vib >= 1.f) vib -= 1.f;
        const float hzv = base_hz * exp2f((cur_st + 0.3f * sinf(2.f * (float)M_PI * vib)) / 12.f);
        ph += hzv / FS;
        if (ph >= 1.f) ph -= 1.f;
        seed = seed * 1664525u + 1013904223u;
        const float noise = (float)(int32_t)seed / 2147483648.f;
        const float tp = 2.f * (float)M_PI * ph;
        /* the procedural voice's tone: harmonics 1..5 at 1/k and an octave partner */
        const float tone = sinf(tp) + 0.5f * sinf(2.f * tp) + 0.33f * sinf(3.f * tp) + 0.25f * sinf(4.f * tp) +
                           0.2f * sinf(5.f * tp) + 0.35f * sinf(2.f * tp);
        float car = voiced ? 0.6f * tone + 0.08f * noise : 0.7f * noise;
        lp_car += (car - lp_car) * lp_k;
        car = lp_car;
        /* bands */
        float out = 0.f;
        for (int b = 0; b < BANDS; b++) {
            const float a = biq(&an[b][1], biq(&an[b][0], x[i]));
            const float mag = a < 0.f ? -a : a;
            env[b] += (mag - env[b]) * (mag > env[b] ? env_k_up : env_k_dn);
            const float s = biq(&sy[b][1], biq(&sy[b][0], car));
            out += s * env[b];
        }
        const float sib = biq(&hp2, biq(&hp1, x[i]));
        y[i] = robot * (out * 6.f + (voiced ? 0.25f : 0.6f) * sib) + (1.f - robot) * x[i];
    }
    /* normalise to -3 dBFS */
    float peak = 1e-6f;
    for (int i = 0; i < n; i++) if (fabsf(y[i]) > peak) peak = fabsf(y[i]);
    for (int i = 0; i < n; i++) pcm[i] = (int16_t)(y[i] / peak * 0.7f * 32767.f);
    wav_write(argv[2], pcm, n);
    int voiced_frames = 0;
    for (int f = 0; f < frames; f++) voiced_frames += hz[f] > 0.f;
    printf("%s: %d ms, %d%% voiced, speech median %.0f Hz\n", argv[2], n * 1000 / FS, voiced_frames * 100 / (frames ? frames : 1), median_hz);
    return 0;
}
