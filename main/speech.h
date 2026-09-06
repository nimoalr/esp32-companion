/*
 * The character's mouth: one task on core 0 that renders the procedural voice
 * (voice.c) and the sampled word clips (clips_gen.c, IMA ADPCM) into the speaker,
 * switching the amplifier on only while something plays. Requests are one at a time:
 * a new one while speaking is dropped unless `interrupt` is set.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "voice.h"
#include "clips_gen.h"

esp_err_t speech_init(void);
/* a mood gesture; level 0..1 (energy) */
bool speech_gesture(voice_id_t id, float level, bool interrupt);
/* a random babble */
bool speech_babble(float level, float energy);
/* a word clip */
bool speech_word(int clip, float level, bool interrupt);
bool speech_busy(void);
bool speech_purring(void);      /* currently rendering a purr; excludes queued/dropped requests */
/* settings */
void speech_set_register(voice_register_t reg);
void speech_set_volume(int pct);
