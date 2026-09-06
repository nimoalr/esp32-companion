/*
 * Microphone capture and beat analysis for the dance mode.
 *
 * ES7210 ADC (MIC1 = left, MIC2 = right) over standard I2S at 16 kHz, 16-bit
 * stereo, configured through esp_codec_dev. An analysis task on core 0 turns
 * 256-sample frames (16 ms) into band levels, an onset detector and a stereo
 * balance; the render task reads the result with audio_get_features().
 * Nothing here runs unless audio_start() was called.
 */
#pragma once

#include "esp_err.h"
#include "audio_features.h"

esp_err_t audio_start(void);
void audio_stop(void);
bool audio_running(void);
/* Microphone gain in dB for the next audio_start (default CONFIG_EYES_AUDIO_GAIN_DB); the mic
 * wizard asks for less so a clap 30 cm away does not clip and its edge stays intact. */
void audio_set_gain_db(int db);
int audio_gain_db(void);
/* Map raw arrival-time differences to the direction value: dir = (lag - offset) * gain. */
void audio_set_dir_cal(const mic_cal_t *cal);
void audio_get_features(audio_features_t *out);
