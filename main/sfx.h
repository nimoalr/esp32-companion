#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef enum { SFX_NONE, SFX_PUCK, SFX_REEL, SFX_STOP, SFX_IMPACT, SFX_GLASS } sfx_id_t;
typedef struct { uint32_t phase,step,noise; int left,total,gain; sfx_id_t id; } sfx_note_t;
typedef struct { sfx_note_t note[3]; unsigned cursor; } sfx_t;
void sfx_start(sfx_t *s,sfx_id_t id,float level);
bool sfx_active(const sfx_t *s);
void sfx_mix(sfx_t *s,int16_t *pcm,int n);
