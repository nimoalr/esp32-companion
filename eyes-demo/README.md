# Eyes render demo

Two orange cartoon eyes on the round 1.75" AMOLED of the Waveshare
ESP32-S3-Touch-AMOLED-1.75. They blink and glance around on their own; a tap
on the screen cycles through eight expressions. Nothing else: no audio, IMU,
RTC, Wi-Fi, Bluetooth, storage or menus, and no graphics library. The renderer
is a custom fixed-point scanline rasteriser feeding the panel through DMA.

Target: **ESP-IDF v5.5.5** (any 5.5.x should work; managed components need
`idf >= 5.5`).

## Build and flash

```sh
git clone <this repo>
cd esp32-companion/eyes-demo
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor      # or /dev/ttyUSB0 / COMx
```

The first build downloads two managed components from the Espressif registry
(`espressif/esp_lcd_co5300` 2.1.0 and `waveshare/esp_lcd_touch_cst9217`
2.0.0; versions are pinned in `dependencies.lock`). The board enumerates as a
USB-Serial/JTAG device. Exit the monitor with `Ctrl+]`.

## What you should see

1. Boot: black screen, then two orange capsule eyes appear.
2. Idle: a blink every 2..6 s (about 120 ms close and open) and a small
   saccade every 1..3 s.
3. Each tap advances the expression, easing over 120..300 ms, in this order:
   NEUTRAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPY, LOOK_AROUND, WINK, then back
   to NEUTRAL on the eighth tap. A tap in the middle of a transition retargets
   immediately from the current shape.
4. The console prints one line per second:

```
I (12345) eyes: NEUTRAL: 60 fps | frame 3210 us avg, 3480 us max (raster+DMA) | 62604 B/frame in 2 rect(s) | pace TE
```

`frame` is the wall time from the V-blank edge until the last DMA transfer of
the frame has completed. `B/frame` is the number of bytes pushed to the panel
per frame; a full frame would be 434 312 B.

A tap is a touch-down followed by a touch-up within 300 ms with less than 10 px
of travel. Holds and drags are ignored; taps closer than 150 ms apart are
debounced.

## Layout

```
eyes-demo/
  CMakeLists.txt            builds `main` only (no Wi-Fi/BT/NVS components in the tree)
  sdkconfig.defaults        esp32s3, 240 MHz, octal PSRAM 80 MHz, 32K/64K caches, -O2, 1 kHz tick
  main/
    idf_component.yml       esp_lcd_co5300 + esp_lcd_touch_cst9217 (component manager reads it here)
    Kconfig.projbuild       QSPI clock choice (40 MHz default, 80 MHz experimental)
    main.c                  init, render task (core 1), dirty rects, stats
    board.h                 pin map and panel constants, each with its source
    display.c/.h            QSPI + CO5300 via esp_lcd, two band buffers, TE sync, 60 Hz fallback
    touch.c/.h              CST9217 via esp_lcd_touch, ISR -> task (core 0) -> tap queue
    raster.c/.h             Q16.16 scanline rasteriser, 4x vertical sampling, coverage LUT
    eyes.c/.h               EyeParams / EyeState, per-field easing, blink + saccade layer
    anim.c/.h               keyframe table for the eight expressions + state machine
  docs/hardware.md          pin sources, TE, QSPI clock, power rail
```

## Rendering architecture

* **No framebuffer.** Two 466 x 32 px RGB565 band buffers (29 824 B each) in
  internal DMA-capable SRAM. Band N is DMA'd while band N+1 is rasterised. A
  counting semaphore, released from the `esp_lcd` transfer-done callback, hands
  a buffer back only after its DMA has finished. PSRAM is enabled but unused
  by the renderer.
* **Dirty rectangles.** Each eye's pixel bounding box for the previous and the
  current frame are unioned, aligned to the panel's 2-pixel granularity, and
  pushed as a partial window (CASET/RASET/RAMWR per band via
  `esp_lcd_panel_draw_bitmap`). The two eyes are pushed as two rects unless
  their rects overlap, in which case they merge. The black background is only
  drawn once at boot. A closed eye shrinks its rect to a few rows.
* **Analytic rasteriser.** An eye is a rounded rectangle (capsule) with a
  straight, optionally slanted top lid line and a bottom lid that can bulge
  upward (the happy arc). Each pixel row is sampled on four sub-scanlines; the
  left/right edge pixels get exact horizontal coverage, the fully covered core
  is written with 32-bit stores, and coverage indexes a 256-entry RGB565 LUT
  (eye colour over black, pre-swapped to panel byte order). Everything on the
  per-row and per-pixel path is integer Q16.16; the whole band rasteriser is
  linked into IRAM.
* **Frame pacing.** TE is wired to GPIO13, so each frame's panel write starts
  on the rising TE edge (V-blank). If TE never pulses, a 60 Hz `esp_timer`
  takes over and the log says `pace timer`. All motion is time based
  (`esp_timer_get_time`), never frame-count based.
* **Tasks.** Render task pinned to core 1 at priority `configMAX_PRIORITIES-3`.
  Touch task on core 0: GPIO ISR -> semaphore -> I2C read -> tap detection ->
  FreeRTOS queue -> render task. The SPI and I2C interrupts live on core 0.

## Eye model

`EyeParams` (`cx, cy, w, h, radius, lid_top, lid_bottom, slant, curve, color`)
is what the rasteriser consumes. `curve` is one field beyond the brief: the
upward bulge of the bottom lid that makes the HAPPY arc. Animations write an
`eye_pose_t` (scale, lids, slant, curve, offset) as keyframes; `EyeState` eases
every field independently with a smoothstep, so a retarget mid-transition
starts from the current value. Blink (vertical squash) and saccades (shared
centre offset) are layered on top for every expression; SLEEPY slows the blink
rate and speed, SURPRISED blinks less.

## Performance

Measured on the host build of the rasteriser (`raster.c`, `eyes.c`, `anim.c`
compile unchanged with gcc): neutral eyes are 2 x 31 302 px per frame, about
62.6 kB pushed per frame, 14 % of a full frame. Bus time at 40 MHz QSPI is
3.1 ms per frame (1.6 ms at 80 MHz); the CPU side is a fraction of that and
overlaps the DMA. The remaining budget at 60 fps is more than 10 ms per frame.
A host sweep through all eight expressions at 60 Hz with blinks and saccades
live averages 58.5 kB per frame and peaks at 106.7 kB (SURPRISED), with no
writes outside the dirty rectangles under AddressSanitizer/UBSan.

On-device frame time and fps were not measured while writing this; there was no
board attached. The per-second log line above reports them live.

## Configuration

`idf.py menuconfig` -> "Eyes demo" -> "CO5300 QSPI clock": 40 MHz (default, the
value all Waveshare reference code uses) or 80 MHz (untested). See
`docs/hardware.md` for the reasoning and the bus-time table.
