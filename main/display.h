#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "board.h"

#define DISPLAY_W           BOARD_LCD_H_RES
#define DISPLAY_H           BOARD_LCD_V_RES
#define DISPLAY_BAND_ROWS   16
#define DISPLAY_BANDS       3       /* internal bounce buffers: frames are copied from PSRAM a band at a time */
#define DISPLAY_FRAME_BYTES ((DISPLAY_W * DISPLAY_H * 2 + 63) & ~63)
#define DISPLAY_BAND_PIXELS (DISPLAY_W * DISPLAY_BAND_ROWS)
#define DISPLAY_BAND_BYTES  (DISPLAY_BAND_PIXELS * 2)

/* Bring up the QSPI bus, CO5300 panel, band buffers and TE input. */
esp_err_t display_init(void);

/*
 * Band buffer ring. display_acquire_band() hands out the DISPLAY_BANDS buffers
 * in turn and blocks until the buffer's previous transfers have completed. A
 * buffer may then be pushed in any number of pieces (e.g. one per eye) before
 * the next acquire of that buffer.
 */
uint16_t *display_acquire_band(void);
/* Queue a band; returns the transfer's sequence number (see display_wait_done). */
uint32_t display_push(int x, int y, int w, int h, const uint16_t *band);

/* Queue a whole internal DMA-capable buffer (w x h pixels, up to DISPLAY_DIRECT_MAX bytes); returns its sequence number. */
#define DISPLAY_DIRECT_MAX  (160 * 1024)
uint32_t display_push_direct(int x, int y, int w, int h, const uint16_t *pixels);

/* Block until the transfer with this sequence number (and all before it) has completed. */
void display_wait_done(uint32_t seq);

/*
 * DMA copy (GDMA memcpy) from anywhere, PSRAM included, into internal memory.
 * Returns false if the engine refused the job (alignment); the caller then
 * copies with the CPU. display_copy_wait() blocks until the last copy landed.
 */
bool display_copy_start(void *dst, const void *src, size_t bytes);
void display_copy_wait(void);
uint32_t display_te_period_us(void);
/* TE edges seen since boot (diagnostic). */
uint32_t display_te_edges(void);
/* Block until `us` after the most recent TE edge (returns at once if that is past or TE is not wired). */
void display_wait_after_te(uint32_t us);

/* Block until every queued transfer has completed. */
void display_wait_idle(void);

/*
 * Wait for the next frame boundary: the TE rising edge when TE is wired and
 * pulsing, otherwise a 60 Hz software timer. Returns false on timeout.
 */
bool display_wait_vsync(uint32_t timeout_ms);
bool display_te_active(void);

/* Panel brightness, 0..100 (CO5300 command 0x51). */
void display_set_brightness(uint8_t percent);

/* Display Off + Sleep In (true) or Sleep Out + Display On (false). Blocking. */
void display_sleep(bool sleep);

/*
 * Low-power frame pacing: block (letting the chip light-sleep) until just
 * before the TE edge that is `period` refresh periods after the last frame.
 * Follow with display_wait_vsync() to lock onto that edge.
 */
void display_delay_until_frame(uint32_t period);

/* Paint the whole panel black through the band path. */
void display_fill_black(void);

/* Bytes pushed to the panel since the previous call. */
uint32_t display_take_bytes(void);

uint32_t display_pclk_hz(void);
