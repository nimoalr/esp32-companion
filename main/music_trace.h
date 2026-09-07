/* Versioned, explicitly little-endian feature trace. No waveform or flash writes. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio_features.h"
#define MUSIC_TRACE_BYTES 24
void music_trace_pack(uint8_t out[MUSIC_TRACE_BYTES],const audio_features_t *a,uint32_t ms,
                      float kick,float mean,float previous,float presence,unsigned flags,unsigned session);
#ifndef AUDIO_ANALYSIS_HOST
#include "esp_err.h"
void music_trace_init(void);
esp_err_t music_trace_enable(bool on);
bool music_trace_active(void);
void music_trace_poll(void); /* bounded nonblocking USB commands, render task only */
void music_trace_context(bool dancing,bool listening);
void music_trace_offer(const audio_features_t *a,uint32_t ms,float kick,float mean,float previous,float presence,unsigned flags);
#endif
