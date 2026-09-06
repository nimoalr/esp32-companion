/* Render the voice vocabulary to WAV: one file per gesture and register, plus a medley per
 * register with all gestures in a row. Listen before any of it goes into the firmware. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "voice.h"

static void wav_write(const char *path, const int16_t *pcm, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t data = (uint32_t)n * 2, rate = VOICE_RATE, brate = rate * 2;
    const uint16_t ch = 1, bits = 16, align = 2, fmt = 1;
    const uint32_t fmtlen = 16, riff = 36 + data;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtlen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm, 2, (size_t)n, f);
    fclose(f);
}

#define MAX_S (VOICE_RATE * 3)

/* render one utterance (a looping one for 1.5 s) into buf; returns samples */
static int render(voice_t *v, voice_id_t id, int16_t *buf)
{
    voice_start(v, id, 1.f);
    int n = 0;
    while (voice_active(v) && n < MAX_S) {
        if (k_voice_gestures[id].loop && n >= VOICE_RATE * 3 / 2) voice_stop(v);
        n += voice_render(v, buf + n, 160);
    }
    return n;
}

int main(void)
{
    static const char *regs[3] = { "low", "mid", "high" };
    mkdir("out", 0755);
    mkdir("out/voice", 0755);
    static int16_t buf[MAX_S], medley[VOICE_COUNT * (MAX_S + VOICE_RATE / 2)];
    voice_t v;
    for (int r = 0; r < 3; r++) {
        voice_init(&v, 12345u + (uint32_t)r);
        voice_set_register(&v, (voice_register_t)r);
        int m = 0;
        for (int id = 0; id < VOICE_COUNT; id++) {
            const int n = render(&v, (voice_id_t)id, buf);
            char path[128];
            snprintf(path, sizeof path, "out/voice/%s_%02d_%s.wav", regs[r], id, k_voice_gestures[id].name);
            wav_write(path, buf, n);
            memcpy(medley + m, buf, sizeof(int16_t) * (size_t)n);
            m += n;
            memset(medley + m, 0, sizeof(int16_t) * (VOICE_RATE / 2));
            m += VOICE_RATE / 2;
            int peak = 0;
            for (int i = 0; i < n; i++) if (abs(buf[i]) > peak) peak = abs(buf[i]);
            if (r == 1) printf("%-10s %5d ms  peak %5d\n", k_voice_gestures[id].name, n * 1000 / VOICE_RATE, peak);
        }
        char path[128];
        snprintf(path, sizeof path, "out/voice/medley_%s.wav", regs[r]);
        wav_write(path, medley, m);
    }
    /* variety: the same gesture five times, mid register, should never sound identical */
    voice_init(&v, 99u);
    int m = 0;
    for (int k = 0; k < 5; k++) {
        const int n = render(&v, VOICE_HAPPY, buf);
        memcpy(medley + m, buf, sizeof(int16_t) * (size_t)n);
        m += n;
        memset(medley + m, 0, sizeof(int16_t) * (VOICE_RATE / 4));
        m += VOICE_RATE / 4;
    }
    wav_write("out/voice/variety_happy.wav", medley, m);
    printf("written to tools/host/out/voice/\n");
    return 0;
}
