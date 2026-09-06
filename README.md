# esp32-companion

A small companion character for the Waveshare ESP32-S3-Touch-AMOLED-1.75C: two
cartoon eyes on the round 1.75" AMOLED that blink and glance around on their
own, react to being handled, tilted, shaken, petted, poked or talked to,
dance to music with light shows inside the eyes, chirp and say short words
through the speaker, dim into an always-on face when left alone and sleep
until picked up. No graphics library: the renderer is a custom fixed-point
scanline rasteriser feeding the panel through DMA, and the whole thing is
plain ESP-IDF C.

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
and what was taken from them. The character picks his own expression from
his mood; changes ease over 120..300 ms and retarget mid-transition. A finger
resting on the screen is attention: the eyes settle on it, open a touch wider
and stop wandering until it lifts (Vector's focus mode). Expressions can also
place and scale the whole face, and the eyes are shaded by a soft hot spot
that follows the gaze:

NEUTRAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPY, LOOK_AROUND, WINK, CURIOUS,
CONFUSED, LOVE, DIZZY, LAUGHING, SCARED, SKEPTICAL, THINKING, BORED, EXCITED,
SHY, ANNOYED, SLEEPING, SQUINT, DANCE, SMUG, SUSPICIOUS, DETERMINED, PLEADING,
MISCHIEVOUS, EMBARRASSED, RELIEVED, DOUBLE_TAKE, KNOCKED_OUT, RECOVERING,
HEARTS, HEARTBREAK, HIGH_ROLLER, NOD, PEEKABOO, LOADING, BOOP, SNEEZE,
CAUTIOUS_PEEK, HIDE_RELOCATE, TOO_CLOSE, RIM_BONK, HANGING_ON, LAZY_PUDDLE,
AROUND_BEND, SECRET_OBSERVER, WRONG_ENTRANCE, JACKPOT_ESCAPE.

See the [character preview guide](docs/expressions/README.md) for the full
catalog, animated previews, and a comparison of the old and new shake reaction.
Heart eyes beat twice, break and mend; slot-machine eyes scroll through clipped
reel windows, stop separately on sevens, then celebrate. Silhouette changes
close the old eyes and reopen the new ones over 280 ms, including interrupted
transitions. These actions are selectable independently of mood. Ten more
[round-display scenes](docs/expressions/RIM_ACTIONS.md) use the edge for hiding,
peeking, clinging, bouncing and orbiting.

Each expression is a keyframe pose per eye (size, position, both lids with
slant and bend, angle, four elliptical corner radii) plus up to three
modulators (a sine or a random jitter on one pose field, or on the face-level
scale and offset) applied every frame on top of the eased pose: the heartbeat in LOVE, the circling gaze in DIZZY, the
bounce in LAUGHING and EXCITED, the tremble in SCARED, the breathing in
SLEEPING. Ordinary expressions live in `main/anim.c`; symbol silhouettes are
small code-defined contours in `main/eye_symbols.c`, using the same shading
and rotation as the capsule eyes.

**Character.** A behaviour layer watches the sensors and decides what the
face does; a persona layer decides what he says about it. Both are pure C
(`behavior.c`, `persona.c`), driven by two mood numbers in Vector's manner:
*stimulation* (rises with anything happening, sinks with quiet) and
*valence* (how well he has been treated lately; petting, company, music and
food when hungry raise it, shaking, knocks and pokes lower it, and it fades
back over minutes). The idle face is rolled from the mood every 15..55 s,
and so are the words he picks.

| Trigger | Reaction |
| --- | --- |
| Tilting | The whole face turns about the screen centre so "up" stays up (past ~17 deg; back upright when flat); the eyes slide toward the low side. |
| Shaking | Dizzy after about a second, passed out after about four (slumped eyes and stars for 8 s, then a 3 s uneven-eye recovery). Protests, then rudeness that escalates if it goes on. Never during a dance. |
| Face down | Eyes close, z's float up; lifting it back gives a surprised wake. |
| Picked up, put down, carried | A small greeting; a settled squint and a purr after a few seconds of walking rhythm. |
| A knock on the shell | Startled, a glance toward the side of the knock, a complaint. |
| Touch | A tap is a poke (on an eye, that eye alone shuts and reopens); a flurry of pokes earns a complaint; strokes across the forehead are petting (love face, purr); a finger resting is attention. |
| Someone talking | Detected from the syllable rhythm of the mid band. A curious or thoughtful face, the eyes lean toward the talker along the microphone axis (calibrated in the setup UI), one short answer, then quiet. |
| Music | A steady tempo with a kick under it, checked continuously on USB and in short sniffs on battery. A dance, or sometimes an unimpressed look. Inside the dance, show pieces come and go on the beat: spectrum bars, a mirror ball, sweeping spotlights, all drawn as fills inside the eye shapes. Touch never interrupts a dance. |
| Charger, battery | A line for every plug and unplug, chosen by how hungry he is; asks to be fed when low. |

**Voice.** Two sources through the ES8311 and the on-board speaker, both in
the low register by default: a procedural synthesiser (`voice.c`) for the
mood chirps and idle babble, and a set of 54 sampled words with attitude
(`clips_gen.c`, IMA ADPCM, rendered on the host by `tools/host/clips.sh`)
from "hello" and "uh-oh" to "seriously?" and "fuck you". Register, chattiness
(quiet to talkative, which paces the idle chatter) and volume are settings.
He never repeats one of his last two lines, and his own voice is held out of
the beat, speech and direction detectors while he speaks. The sound design
itself is being reworked separately; see `docs/voice/HANDOFF.md`.

**Colour.** The eye colour is a setting (eight named colours, chosen in the
setup UI, persisted in NVS) and stays the character's identity. Expressions
tint it: a little brighter when happy or excited, dim and washed out when
sad, sleepy or asleep, pulled toward red when angry, toward pink when in
love or shy, toward a sickly green when dizzy, pale when scared. Mood dims
and desaturates a tired character; the dance flashes the colour on each
beat. The rasteriser only ever sees a 256-entry colour LUT, rebuilt when the
colour actually changes.

Props (stars, z's, the charge gauge) are drawn through the same band
path as the eyes and rotate with the face.

**Dance.** Reached by itself when music is detected, or by hand from the
setup menu. The bass makes the eyes grow and squash, each beat makes them
jump with an alternating lean, loudness bends the bottom lids into a smile,
the gaze turns toward the louder side, and when the room goes quiet they
settle back. Beats count as company for the power state machine, so a
device playing along does not fall asleep.

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

**Setup UI.** Hold the PWR button for 2 s to open a small setup menu
styled like a terminal UI and laid out for the disc: title with a rule, a list
with an inverted selection bar, bracketed progress bars, a thin ring at the
edge, hints along the bottom. Navigation is single-touch: swipe up/down to
move, tap to select, hold (or swipe right) to go back. The menu leaves after
60 s without input (`EYES_UI_TIMEOUT_S`).

| Screen | What it does |
| --- | --- |
| Calibrate accel | Three poses (flat screen-up, upright USB-down, resting on the left edge), 2 s of stillness each with a `[####....]` bar. Computes per-axis bias and scale and the sensor-to-screen frame, shows the result, tap saves it to NVS. Runs automatically on first boot. |
| Calibrate mics | Three places, three claps each, no tapping: with the device flat, clap 30 cm from the USB end, from the lanyard end, then above the screen. Each clap gives an arrival-time difference and a level difference between the mics; the ends give sign and scale, the front the zero. Claps are refused while the device is handled or when they clip. The run is replayed into the log when USB returns, so it can be done on battery. |
| Level | Ball on cross-hairs driven by the calibrated accelerometer, pitch and roll in degrees. Turns green when level. |
| Brightness | Tap or swipe cycles 25/50/75/100 %; applied live, saved on exit. |
| Voice | Register (low / mid / high), chattiness (quiet to talkative), volume, and two rows that play a sample word or a sample chirp. |
| Battery | Percentage, voltage, charge state, the rim gauge, and two learned lines: the time left on this charge (or until full while charging) at the rate measured over the last five discharge or charge stretches, and what a full charge and a refill take on average. Stretches shorter than 3 % or 5 min are ignored; the running stretch is saved to NVS every ten minutes. |
| Back to eyes | Leave the menu. |

Text is Spleen (BSD-2-Clause, `tools/bdf2c.py` converts the BDF files into
`main/font_spleen_*.c`). The setup screens open with a 2 s hold of the PWR button and close the same way. The whole glass belongs to the character: a tap is a poke (on an eye, that eye shuts), a stroke across the forehead is petting, a finger resting is attention.

**Persistence.** Brightness, eye colour, voice settings, both calibrations
and the battery statistics live in NVS (namespace `companion`); the mood does
not, he wakes neutral. `idf.py erase-flash` brings back the first-boot wizard.

**Console.** One line per second:

```
I (12345) eyes: ACTIVE HAPPY [idle, energy 0.52, valence +0.31]: 60 fps | raster 3210 us avg, 3480 us max | push 1340 us | 62604 B/frame, 2 rect(s) | pace TE (60 TE/s) | bri 100% | batt 3987 mV 87% chg usb | audio ... | stack 11068 B free
```

Power state, expression (or setup screen), behaviour state and mood, then
the frame path, the panel pacing, brightness, the AXP2101 fuel gauge, the
microphone features while the mics run, and the render task's stack margin.
Every utterance is logged by the `speech` tag, every wizard clap by `ui`.

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
  raster.c/.h             Q16.16 scanline rasteriser, 4x vertical sampling, coverage LUT, hot spot, dance fill effects
  eyes.c/.h               EyeParams / EyeState, per-field easing, blink + dart layer, squash/stretch, gaze scaling
  anim.c/.h               51 expressions/actions, transitions and dance choreography
  eye_symbols.c/.h        heart, broken-heart and scrolling reel contours
  audio.c/.h              ES7210 mics and ES8311 speaker over I2S via esp_codec_dev; FFT, bands, beats, tempo, speech, direction
  micdir.c/.h             arrival-time difference of a transient between the two mics; pure C, host-tested
  behavior.c/.h           reactions (shake, face-down, handling, knocks, touch, talking, music), mood, gravity face; pure C
  persona.c/.h            what he says and when: situational lines, answers, idle chatter paced by chattiness; pure C
  voice.c/.h              procedural voice: syllables, vowels, onsets, vibrato, breath, babble; pure C
  speech.c/.h             the mouth: a task mixing the voice and the word clips into the speaker, amplifier switched
  adpcm.c/.h              IMA ADPCM codec for the clips; clips_gen.c/.h are generated by tools/host/clips.sh
  battstat.c/.h           learned discharge and charge rates from the last five stretches; pure C
  accessories.c/.h        stars, z's, the charge gauge; rotate with the face; pure C
  power.c/.h              ACTIVE / DROWSY / SLEEP / DEEP, motion detector, PM locks, sleep
  imu.c/.h                QMI8658: accelerometer polling, hardware wake-on-motion
  pmic.c/.h               AXP2101: battery telemetry, USB detection, the PWR button, soft power off
  i2c_bus.c/.h            the one shared I2C bus
  settings.c/.h           brightness, eye colour, voice settings, both calibrations in NVS, Kconfig defaults
  ui.c/.h                 setup screens (menu, two calibration wizards, level, brightness, eye colour, voice, battery); pure C
  gfx.c/.h                band-clipped primitives: fill, text, anti-aliased disc and ring/arc
  font_spleen_*.c         Spleen 8x16 / 12x24 / 16x32 glyph tables (generated)
  imu_cal.c/.h            three-pose accelerometer calibration and screen-frame tilt; pure C
tools/bdf2c.py            BDF -> C glyph table converter
tools/host/               desktop builds of the pure-C modules: renders, benchmarks, tests, the voice and clip pipelines
docs/hardware.md          pin sources, TE, QSPI clock, power rails, microphones, battery budget
docs/personality.md       the personality engine: senses, drives, arbitration, voice, visuals, order of work
docs/voice/               the voice: rendered candidates and the handoff brief for the sound design
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
(1.6 ms at 80 MHz). The current host sweep covers all 51 animations twice,
including rotation, increased face scale, shading and attention: 44,676 frames,
112.1 kB average and 474.1 kB peak rectangle traffic, with no bbox/guard
violations under UBSan. These deliberately enlarged/rotated cases are not
on-board frame-time measurements. Overlaid
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
