#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "board.h"

#define DISPLAY_W           BOARD_LCD_H_RES
#define DISPLAY_H           BOARD_LCD_V_RES
#define DISPLAY_BAND_ROWS   32
#define DISPLAY_BAND_PIXELS (DISPLAY_W * DISPLAY_BAND_ROWS)
#define DISPLAY_BAND_BYTES  (DISPLAY_BAND_PIXELS * 2)

/* Bring up the QSPI bus, CO5300 panel, band buffers and TE input. */
esp_err_t display_init(void);

/*
 * Band buffer ping-pong. Every display_acquire_band() must be followed by
 * exactly one display_push() of that buffer. acquire blocks until one of the
 * two buffers has finished its previous DMA transfer.
 */
uint16_t *display_acquire_band(void);
void display_push(int x, int y, int w, int h, const uint16_t *band);

/* Block until every queued transfer has completed. */
void display_wait_idle(void);

/*
 * Wait for the next frame boundary: the TE rising edge when TE is wired and
 * pulsing, otherwise a 60 Hz software timer. Returns false on timeout.
 */
bool display_wait_vsync(uint32_t timeout_ms);
bool display_te_active(void);

/* Paint the whole panel black through the band path. */
void display_fill_black(void);

/* Bytes pushed to the panel since the previous call. */
uint32_t display_take_bytes(void);

uint32_t display_pclk_hz(void);
