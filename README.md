# esp32-companion

A small companion character for the Waveshare ESP32-S3-Touch-AMOLED-1.75C: two
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

**Eyes.** Two orange capsules blink every 3..10 s and dart to a new spot
every 1..2.25 s, with the secondary motion that makes them read as alive:
they squash on the way through a dart, widen into a sliver as they close,
grow when looking up and shrink when looking down, stretch along fast moves
and overshoot a little on expressive changes. The timing and proportions
follow Anki Vector's procedural face; see `docs/animation.md` for the sources
and what was taken from them. A tap (or a left swipe; right swipe goes back)
cycles through 23 expressions, easing over 120..300 ms; a tap mid-transition
retargets from the current shape. A finger resting on the screen is
attention: the eyes settle on it, open a touch wider and stop wandering
until it lifts (Vector's focus mode). Expressions can also place and scale
the whole face, and the eyes can be shaded by a soft hot spot that follows
the gaze (off by default, toggled in the setup menu):

NEUTRAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPY, LOOK_AROUND, WINK, CURIOUS,
CONFUSED, LOVE, DIZZY, LAUGHING, SCARED, SKEPTICAL, THINKING, BORED, EXCITED,
SHY, ANNOYED, SLEEPING, SQUINT, DANCE.

Each expression is a keyframe pose per eye (size, position, both lids with
slant and bend, angle, four elliptical corner radii) plus up to three
modulators (a sine or a random jitter on one pose field, or on the face-level
scale and offset) applied every frame on top of the eased pose: the heartbeat in LOVE, the circling gaze in DIZZY, the
bounce in LAUGHING and EXCITED, the tremble in SCARED, the breathing in
SLEEPING. Adding an expression is one table entry in `main/anim.c`.

**Character.** A behaviour layer watches the environment and overrides the
tapped expression while a reaction lasts:

| Trigger | Reaction |
| --- | --- |
| Tilting the device | The whole face turns about the screen centre so "up" stays up, like a badge on a wheel hub (past ~17 deg of tilt; back upright when flat). Within that, the eyes slide a little toward the low side, and slight handling adds a small wobble. |
| Shaking | Dizzy after about a second; passing out after about four: X eyes, stars circling overhead for 8 s, then a groggy few seconds. |
| Face down on the table | Eyes close, z's float up; lifting it back gives a short surprised wake. |
| Music | Every 20 s while awake the mics listen for 3 s. Regular beats trigger a reaction rolled against his mood: usually a dance, sometimes an unimpressed look before ignoring the music for a while. Quiet or a tap ends it. |

**Colour.** The eye colour is a setting (eight named colours, chosen in the
setup UI, persisted in NVS) and stays the character's identity. Expressions
tint it: a little brighter when happy or excited, dim and washed out when
sad, sleepy or asleep, pulled toward red when angry, toward pink when in
love or shy, toward a sickly green when dizzy, pale when scared. Mood dims
and desaturates a tired character; the dance flashes the colour on each
beat. The rasteriser only ever sees a 256-entry colour LUT, rebuilt when the
colour actually changes.

Mood is a single "energy" value that rises when he is handled or touched,
sinks with time and wanders a little; it biases the music reaction and can
drive more later. Props (stars, X eyes, z's) are drawn through the
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

**Setup UI.** Swipe down over the eyes to open a small setup menu
styled like a terminal UI and laid out for the disc: title with a rule, a list
with an inverted selection bar, bracketed progress bars, a thin ring at the
edge, hints along the bottom. Navigation is single-touch: swipe up/down to
move, tap to select, hold (or swipe right) to go back. The menu leaves after
60 s without input (`EYES_UI_TIMEOUT_S`).

| Screen | What it does |
| --- | --- |
| Calibrate accel | Three poses (flat screen-up, upright USB-down, resting on the left edge), 2 s of stillness each with a `[####....]` bar. Computes per-axis bias and scale and the sensor-to-screen frame, shows the result, tap saves it to NVS. Runs automatically on first boot. |
| Calibrate mics | Three places, three claps each, no tapping: clap 30 cm from the USB end, from the lanyard end, then in front of the screen. Each clap is timed by its rising edge in both microphones; the two ends give the sign and scale of the microphone axis, the front gives its zero. The result screen shows the live arrow (up = lanyard end) for a check before saving. Every clap is logged with its raw lag and per-mic level. |
| Level | Ball on cross-hairs driven by the calibrated accelerometer, pitch and roll in degrees. Turns green when level. |
| Brightness | Tap or swipe cycles 25/50/75/100 %; applied live, saved on exit. |
| Battery | Percentage, voltage, charge state, the rim gauge, and two learned lines: the time left on this charge (or until full while charging) at the rate measured over the last five discharge or charge stretches, and what a full charge and a refill take on average. Stretches shorter than 3 % or 5 min are ignored; the running stretch is saved to NVS every ten minutes. |
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
  touch.c/.h              CST9217 via esp_lcd_touch, ISR -> task (core 0) -> gesture queue (tap, swipes) + finger position
  raster.c/.h             Q16.16 scanline rasteriser, 4x vertical sampling, coverage LUT
  eyes.c/.h               EyeParams / EyeState, per-field easing, blink + dart layer, squash/stretch, gaze scaling
  anim.c/.h               keyframe + modulator table for 23 expressions, dance choreography
  audio.c/.h              ES7210 microphones over I2S via esp_codec_dev; 256-point FFT, bands, onsets, balance
  behavior.c/.h           environment reactions: shake/knock-out, face-down, music sniffing, mood, gravity face; pure C
  accessories.c/.h        stars, X eyes, z's; rotate with the face; pure C
  power.c/.h              ACTIVE / DROWSY / SLEEP / DEEP, motion detector, PM locks, sleep
  imu.c/.h                QMI8658: accelerometer polling, hardware wake-on-motion
  pmic.c/.h               AXP2101: battery telemetry, soft power off
  i2c_bus.c/.h            the one shared I2C bus
  settings.c/.h           brightness, eye colour palette + calibration in NVS, Kconfig defaults
  ui.c/.h                 setup screens (menu, calibration wizard, level, brightness, eye colour, battery); pure C
  gfx.c/.h                band-clipped primitives: fill, text, anti-aliased disc and ring/arc
  font_spleen_*.c         Spleen 8x16 / 12x24 / 16x32 glyph tables (generated)
  imu_cal.c/.h            three-pose accelerometer calibration and screen-frame tilt; pure C
tools/bdf2c.py            BDF -> C glyph table converter
docs/hardware.md          pin sources, TE, QSPI clock, power rails, battery budget
```

## Host renders

`tools/host/` builds the rendering and animation code with a desktop gcc and
renders expression sheets, motion strips and timings to PPM/PNG without a
board. See `tools/host/README.md`.

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
* **Analytic rasteriser.** An eye is a rounded rectangle with an independent,
  optionally elliptical radius per corner, and two lids that can each be
  slanted and bent (the happy arc is a bent bottom lid). Each pixel row is sampled on four sub-scanlines; edge pixels
  get exact horizontal coverage, the covered core is written with 32-bit
  stores, and coverage indexes a 256-entry RGB565 LUT pre-swapped to panel
  byte order. Everything per row and per pixel is integer Q16.16; the band
  rasteriser is linked into IRAM. Rotated faces use the same scheme: the
  scanline through a rotated rounded rectangle is computed in closed form
  from the nine Voronoi regions of its core (a square root only in the four
  corner regions), straight lids stay half-planes and the arc and the bent
  lid are quadratics, all evaluated once per sub-scanline in float and fed
  to the same coverage code. Unequal corner radii split the scanline at the
  eye's local axes so each quadrant uses its own radius. On the host a
  rotated frame costs about 2.2x an upright one, mostly because the rotated
  bounding box is larger; a bent lid on an upright eye costs about 10 %.
* **Hot spot.** A separable Gaussian lightness falloff in screen
  space: two 466-entry per-axis tables per eye are refilled each frame, and
  every covered pixel does one multiply, a 2x2 ordered dither and two table
  lookups into a 32 lightness x 64 coverage RGB565 LUT (4 KB per eye,
  rebuilt only when the colour changes). About 1.7x the neutral frame on the
  host while the eyes move, nothing when the frame is unchanged.
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
(1.6 ms at 80 MHz). A sweep through all 23 expressions at 60 Hz, once
upright and once with the face turning, with blinks and darts live, averages
78.7 kB per frame and peaks at 231 kB (SURPRISED while rotated), with no
writes outside the dirty rectangles under AddressSanitizer/UBSan. Overlaid
props (`raster_shapes_over`, used by `gfx_disc` and `gfx_line`) blend their
anti-aliased edges over the pixels already in the band, so thick strokes
built from overlapping discs stay solid.

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
