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

The first build downloads three managed components from the Espressif
registry (`espressif/esp_lcd_co5300`, `waveshare/esp_lcd_touch_cst9217`,
`espressif/esp_codec_dev`; versions are pinned in `dependencies.lock`). The board enumerates as a
USB-Serial/JTAG device. Exit the monitor with `Ctrl+]`.

Tunables live under `idf.py menuconfig` -> "Eyes" (display clock and
brightness, power timeouts, thresholds, CPU clock, deep state). Defaults are in
`sdkconfig.defaults`.

## What it does

**Eyes.** Two orange capsules blink every 2..6 s and make a small saccade every
1..3 s. A tap (or a left swipe; right swipe goes back) cycles through 22
expressions, easing over 120..300 ms; a tap mid-transition retargets from the
current shape:

NEUTRAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPY, LOOK_AROUND, WINK, CURIOUS,
CONFUSED, LOVE, DIZZY, LAUGHING, SCARED, SKEPTICAL, THINKING, BORED, EXCITED,
SHY, ANNOYED, SLEEPING, DANCE.

Each expression is a keyframe pose per eye plus up to three modulators
(a sine or a random jitter on one pose field) applied every frame on top of
the eased pose: the heartbeat in LOVE, the circling gaze in DIZZY, the
bounce in LAUGHING and EXCITED, the tremble in SCARED, the breathing in
SLEEPING. Adding an expression is one table entry in `main/anim.c`.

**Character.** A behaviour layer watches the environment and overrides the
tapped expression while a reaction lasts:

| Trigger | Reaction |
| --- | --- |
| Tilting the device | The whole face turns about the screen centre so "up" stays up, like a badge on a wheel hub (past ~17 deg of tilt; back upright when flat). Within that, the eyes slide a little toward the low side, and slight handling adds a small wobble. |
| Shaking | Dizzy after about a second; passing out after about four: X eyes, stars circling overhead for 8 s, then a groggy few seconds. |
| Face down on the table | Eyes close, z's float up; lifting it back gives a short surprised wake. |
| Music | Every 20 s while awake the mics listen for 3 s. Regular beats trigger a reaction rolled against his mood: usually a dance, headphones sliding on more often when he is energetic, sometimes an unimpressed look before ignoring the music for a while. Quiet or a tap ends it. |

Mood is a single "energy" value that rises when he is handled or touched,
sinks with time and wanders a little; it biases the music reaction and can
drive more later. Props (headphones, stars, X eyes, z's) are drawn through the
same band path as the eyes and rotate with the face.

**Dance mode.** The last expression (also reachable from the setup menu)
switches the two microphones on and lets the eyes follow the music: the
bass level makes them grow and squash, each beat makes them jump with an
alternating lean, loudness bends the bottom lids into a smile, the gaze
turns toward the louder side, and when the room goes quiet they settle back
and eventually get heavy lids. Leaving the expression switches the
microphones off. Beats also count as company for the power state machine,
so a device playing along does not fall asleep.

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

**Setup UI.** Hold a finger on the eyes for 0.7 s to open a small setup menu
styled like a terminal UI and laid out for the disc: title with a rule, a list
with an inverted selection bar, bracketed progress bars, a thin ring at the
edge, hints along the bottom. Navigation is single-touch: swipe up/down to
move, tap to select, hold (or swipe right) to go back. The menu leaves after
60 s without input (`EYES_UI_TIMEOUT_S`).

| Screen | What it does |
| --- | --- |
| Calibrate accel | Three poses (flat screen-up, upright USB-down, resting on the left edge), 2 s of stillness each with a `[####....]` bar. Computes per-axis bias and scale and the sensor-to-screen frame, shows the result, tap saves it to NVS. Runs automatically on first boot. |
| Level | Ball on cross-hairs driven by the calibrated accelerometer, pitch and roll in degrees. Turns green when level. |
| Brightness | Tap or swipe cycles 25/50/75/100 %; applied live, saved on exit. |
| Battery | Voltage, percent, charge state; the outer ring is the gauge. |
| Back to eyes | Leave the menu. |

Text is Spleen (BSD-2-Clause, `tools/bdf2c.py` converts the BDF files into
`main/font_spleen_*.c`). In the eyes, a swipe left/right also steps through
the expressions.

**Persistence.** Brightness and the accelerometer calibration live in NVS
(namespace `companion`). `idf.py erase-flash` brings back the first-boot
wizard.

**Console.** One line per second:

```
I (12345) eyes: ACTIVE NEUTRAL: 60 fps | frame 3210 us avg, 3480 us max | 62604 B/frame, 2 rect(s) | pace TE | bri 100% | batt 3987 mV 87% chg usb
```

In the setup UI the expression name is replaced by the screen name.

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
  touch.c/.h              CST9217 via esp_lcd_touch, ISR -> task (core 0) -> gesture queue (tap, hold, swipes)
  raster.c/.h             Q16.16 scanline rasteriser, 4x vertical sampling, coverage LUT
  eyes.c/.h               EyeParams / EyeState, per-field easing, blink + saccade layer
  anim.c/.h               keyframe + modulator table for 22 expressions, dance choreography
  audio.c/.h              ES7210 microphones over I2S via esp_codec_dev; 256-point FFT, bands, onsets, balance
  behavior.c/.h           environment reactions: shake/knock-out, face-down, music sniffing, mood, gravity face; pure C
  accessories.c/.h        headphones, stars, X eyes, z's; rotate with the face; pure C
  power.c/.h              ACTIVE / DROWSY / SLEEP / DEEP, motion detector, PM locks, sleep
  imu.c/.h                QMI8658: accelerometer polling, hardware wake-on-motion
  pmic.c/.h               AXP2101: battery telemetry, soft power off
  i2c_bus.c/.h            the one shared I2C bus
  settings.c/.h           brightness + calibration in NVS, Kconfig defaults
  ui.c/.h                 setup screens (menu, calibration wizard, level, brightness, battery); pure C
  gfx.c/.h                band-clipped primitives: fill, text, anti-aliased disc and ring/arc
  font_spleen_*.c         Spleen 8x16 / 12x24 / 16x32 glyph tables (generated)
  imu_cal.c/.h            three-pose accelerometer calibration and screen-frame tilt; pure C
tools/bdf2c.py            BDF -> C glyph table converter
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
  rasteriser is linked into IRAM. Rotated faces use the same scheme: the
  scanline through a rotated rounded rectangle is computed in closed form
  from the nine Voronoi regions of its core (a square root only in the four
  corner regions), lids stay half-planes and the arc is a quadratic, all
  evaluated once per sub-scanline in float and fed to the same coverage code.
  On the host a rotated frame costs about 2.8x an upright one, mostly because
  the rotated bounding box is larger.
* **Frame pacing.** TE is wired to GPIO13, so each frame's panel write starts
  on the rising TE edge. In DROWSY the task sleeps until ~2.5 ms before the
  Nth predicted edge, then locks onto the real edge. If TE never pulses, a
  60 Hz `esp_timer` takes over and the log says `pace timer`. All motion is
  time based, never frame-count based.
* **Tasks.** Render task pinned to core 1; it is the only task that touches
  the panel, the IMU and the PMIC. Touch task on core 0: GPIO ISR ->
  semaphore -> I2C read -> gesture detector -> queue. SPI and I2C interrupts
  live on core 0. UI screens paint into the same band buffers through
  `gfx.h`, so a menu redraw and an eye blink go down the identical DMA path.

## Performance

Host build of the rasteriser (`raster.c`, `eyes.c`, `anim.c` compile unchanged
with gcc): neutral eyes are 2 x 31 302 px per frame, about 62.6 kB pushed per
frame, 14 % of a full frame. Bus time at 40 MHz QSPI is 3.1 ms per frame
(1.6 ms at 80 MHz). A sweep through all eight expressions at 60 Hz with
blinks and saccades live averages 58.5 kB per frame and peaks at 106.7 kB
(SURPRISED), with no writes outside the dirty rectangles under
AddressSanitizer/UBSan.

Audio analysis runs in its own task on core 0 and never touches the render
task: 16 kHz stereo, 256-sample frames (16 ms), a hand-written radix-2 FFT
with precomputed twiddles and a Hann window, band energies from squared
magnitudes (one square root per band, none per bin), automatic gain per band,
and a bass-onset detector with a 240 ms refractory period. The stats line
shows its per-frame CPU time (`audio N us/frame`) and the estimated tempo.
The choreography itself is a few dozen float operations per frame in the
render task.

Every UI screen is rendered on the host by the same code (`ui.c`, `gfx.c`,
`raster.c` compile unchanged with gcc), which is how the layouts were checked.

On-device frame time, fps and sleep currents have not been measured yet. The
per-second log line reports the first two live; `docs/hardware.md` has the
battery-life estimates that the measurements will replace. Touch mirroring
follows the Waveshare BSP (`EYES_TOUCH_MIRROR_X/Y`); if swipes come out
reversed on your unit, flip them in menuconfig.
