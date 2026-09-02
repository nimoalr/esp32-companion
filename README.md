# esp32-companion

A small companion character for the Waveshare ESP32-S3-Touch-AMOLED-1.75: two
orange cartoon eyes on the round 1.75" AMOLED that blink and glance around on
their own, change expression when tapped, dim into an always-on face when left
alone, and sleep until the device is picked up. No graphics library: the
renderer is a custom fixed-point scanline rasteriser feeding the panel through
DMA, and the whole thing is plain ESP-IDF C.

Target: **ESP-IDF v5.5.5** (any 5.5.x should work; managed components need
`idf >= 5.5`). Hardware notes, pin sources and power budget:
[`docs/hardware.md`](docs/hardware.md).

## Build and flash

```sh
git clone <this repo> esp32-companion
cd esp32-companion
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor      # or /dev/ttyUSB0 / COMx
```

The first build downloads two managed components from the Espressif registry
(`espressif/esp_lcd_co5300` 2.1.0 and `waveshare/esp_lcd_touch_cst9217`
2.0.0; versions are pinned in `dependencies.lock`). The board enumerates as a
USB-Serial/JTAG device. Exit the monitor with `Ctrl+]`.

Tunables live under `idf.py menuconfig` -> "Eyes" (display clock and
brightness, power timeouts, thresholds, CPU clock, deep state). Defaults are in
`sdkconfig.defaults`.

## What it does

**Eyes.** Two orange capsules blink every 2..6 s and make a small saccade every
1..3 s. A tap cycles NEUTRAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPY, LOOK_AROUND,
WINK and back, easing over 120..300 ms; a tap mid-transition retargets from the
current shape. A tap is a touch-down and touch-up within 300 ms with under
10 px of travel; holds and drags are ignored.

**Power states.** Driven by touch and by the QMI8658 accelerometer, which is
polled at 20 Hz while awake to detect the device being handled (deviation from
the gravity vector above `EYES_MOTION_MG`, default 80 mg, on two consecutive
samples).

| State | Trigger | Display | CPU / sleep |
| --- | --- | --- | --- |
| ACTIVE | boot, touch, motion | full brightness (`EYES_BRIGHTNESS_ACTIVE`, 100 %), 60 fps | max clock, no light sleep |
| DROWSY | 30 s without touch or motion | dimmed always-on (`EYES_BRIGHTNESS_AOD`, 10 %), sleepy face, 15 fps | DFS to 40 MHz, automatic light sleep between frames |
| SLEEP | 5 min in DROWSY without activity | panel Display Off + Sleep In | light sleep; IMU wake-on-motion (INT2 -> GPIO21) or touch (GPIO11) wakes to ACTIVE |
| DEEP | 1 h in SLEEP | off | AXP2101 power off: only the PWR button (or plugging USB) restarts the board |

Any touch or motion in DROWSY returns to ACTIVE with the previous expression.
Waking from SLEEP takes about 250 ms: Sleep Out, a black clear, then the eyes
open from closed. The DEEP state can be switched to ESP32 deep sleep with IMU
wake in menuconfig; the BOOT button is a strapping pin and is deliberately not
used as a wake button. Holding PWR for 6 s forces the PMIC off at any time
(AXP2101 hardware default). The state machine can be disabled entirely
(`EYES_POWER_ENABLE=n`) for benchmarking.

**Console.** One line per second:

```
I (12345) eyes: ACTIVE NEUTRAL: 60 fps | frame 3210 us avg, 3480 us max | 62604 B/frame, 2 rect(s) | pace TE | bri 100% | batt 3987 mV 87% chg usb
```

`frame` is the wall time from the V-blank edge until the last DMA transfer of
the frame completed; a full frame would be 434 312 B. Battery numbers come from
the AXP2101 fuel gauge.

## Layout

```
CMakeLists.txt            builds `main` only (no Wi-Fi/BT/NVS components in the tree)
sdkconfig.defaults        esp32s3, 240 MHz, octal PSRAM 80 MHz, caches, -O2, 1 kHz tick, PM + tickless idle
main/
  idf_component.yml       esp_lcd_co5300 + esp_lcd_touch_cst9217
  Kconfig.projbuild       "Eyes" menu: display and power options
  main.c                  render task (core 1): state transitions, dirty rects, stats
  board.h                 pin map, I2C addresses, panel constants, each with its source
  display.c/.h            QSPI + CO5300 via esp_lcd, band buffers, TE sync, brightness, sleep
  touch.c/.h              CST9217 via esp_lcd_touch, ISR -> task (core 0) -> tap queue
  raster.c/.h             Q16.16 scanline rasteriser, 4x vertical sampling, coverage LUT
  eyes.c/.h               EyeParams / EyeState, per-field easing, blink + saccade layer
  anim.c/.h               keyframe table for the eight expressions + state machine
  power.c/.h              ACTIVE / DROWSY / SLEEP / DEEP, motion detector, PM locks, sleep
  imu.c/.h                QMI8658: accelerometer polling, hardware wake-on-motion
  pmic.c/.h               AXP2101: battery telemetry, soft power off
  i2c_bus.c/.h            the one shared I2C bus
  settings.c/.h           runtime settings (brightness), Kconfig defaults, no persistence yet
docs/hardware.md          pin sources, TE, QSPI clock, power rails, battery budget
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
  `esp_lcd_panel_draw_bitmap`). The black background is drawn once at boot and
  after every wake from SLEEP.
* **Analytic rasteriser.** An eye is a rounded rectangle (capsule) with a
  straight, optionally slanted top lid and a bottom lid that can bulge upward
  (the happy arc). Each pixel row is sampled on four sub-scanlines; edge pixels
  get exact horizontal coverage, the covered core is written with 32-bit
  stores, and coverage indexes a 256-entry RGB565 LUT pre-swapped to panel
  byte order. Everything per row and per pixel is integer Q16.16; the band
  rasteriser is linked into IRAM.
* **Frame pacing.** TE is wired to GPIO13, so each frame's panel write starts
  on the rising TE edge. In DROWSY the task sleeps until ~2.5 ms before the
  Nth predicted edge, then locks onto the real edge. If TE never pulses, a
  60 Hz `esp_timer` takes over and the log says `pace timer`. All motion is
  time based, never frame-count based.
* **Tasks.** Render task pinned to core 1; it is the only task that touches
  the panel, the IMU and the PMIC. Touch task on core 0: GPIO ISR ->
  semaphore -> I2C read -> tap detector -> queue. SPI and I2C interrupts live
  on core 0.

## Performance

Host build of the rasteriser (`raster.c`, `eyes.c`, `anim.c` compile unchanged
with gcc): neutral eyes are 2 x 31 302 px per frame, about 62.6 kB pushed per
frame, 14 % of a full frame. Bus time at 40 MHz QSPI is 3.1 ms per frame
(1.6 ms at 80 MHz). A sweep through all eight expressions at 60 Hz with
blinks and saccades live averages 58.5 kB per frame and peaks at 106.7 kB
(SURPRISED), with no writes outside the dirty rectangles under
AddressSanitizer/UBSan.

On-device frame time, fps and sleep currents have not been measured yet. The
per-second log line reports the first two live; `docs/hardware.md` has the
battery-life estimates that the measurements will replace.
