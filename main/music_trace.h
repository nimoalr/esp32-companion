/* Versioned feature trace. No waveform or device flash writes. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "audio_features.h"
#define MUSIC_TRACE_BYTES 64
#define MUSIC_TRACE_BATCH 8
#define MUSIC_TRACE_LINE_BYTES (4+MUSIC_TRACE_BATCH*MUSIC_TRACE_BYTES*2+10)
void music_trace_pack(uint8_t out[MUSIC_TRACE_BYTES],const audio_features_t *a,uint32_t ms,
                      float kick,float mean,float previous,float presence,unsigned flags,unsigned session,
                      const float power[16],uint32_t sequence,uint32_t cpu_us);
size_t music_trace_line(char *out,const uint8_t *records,unsigned count);
#ifndef AUDIO_ANALYSIS_HOST
#include "esp_err.h"
void music_trace_init(void);
esp_err_t music_trace_enable(bool on);
bool music_trace_active(void);
bool music_trace_take_benchmark(void);
void music_trace_poll(void); /* bounded nonblocking USB commands, control/render task only */
void music_trace_context(bool dancing,bool listening);
void music_trace_offer(const audio_features_t *a,uint32_t ms,float kick,float mean,float previous,float presence,unsigned flags,
                       const float power[16],uint32_t cpu_us);
#endif
