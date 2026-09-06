# Host harnesses

The rasteriser, eye model, animation tables, gfx primitives, behaviour and
setup UI compile unchanged with a desktop gcc against the stubs in `stubs/`.
These programs render frames to PPM files and print timings, so a change
can be looked at without a board.

```sh
tools/host/build.sh motion_harness && (cd tools/host && bin/motion_harness)
python3 tools/host/tile.py sheet.png 6 2 tools/host/out/e_*.ppm   # cols, downscale
```

| Harness | What it renders |
| --- | --- |
| `harness` | the original eight expressions, settled and mid-transition |
| `anim_harness` | the newer expressions and a synthetic dance sequence |
| `motion_harness` | every expression, then blink, dart, surprise and look strips |
| `color_harness` | expression tints on three base colours and the mood extremes |
| `feat_harness` | hot spot on/off, gaze-following hot spot, attention, face scale; prints per-frame timings |
| `char_harness` | accessories and rotated faces, plus a behaviour simulation printout |
| `ui_harness` | every setup screen with synthetic sensor data |
| `imu_cal_test` | accelerometer calibration maths: the three wizard poses on five sensor mountings, rejected poses, motion restart; exits non-zero on failure |
| `sweep` | 60 Hz sweep through every expression with rotation, hot spot, face scale and attention; asserts nothing is drawn outside a shape's bounding box. Build with `-fsanitize=address,undefined` |
| `rot_bench`, `shape_bench` | rasteriser timings, upright vs rotated and per shape feature |
| `drafts/hp*_harness` | headphone design drafts (none adopted) |

Timings are host numbers; use them for ratios, not for absolute budgets.

`micdir_test`: synthetic claps with known fractional delays (both polarities, on silence and over loud background music, inside a frame and straddling a frame boundary, clipped and not) through `main/micdir.c`; every clap must be timed exactly once within 0.15 sample.

`voice_render`: writes the procedural voice vocabulary (`main/voice.c`) to `out/voice/`: one WAV per gesture and register, `moods_<reg>.wav` and `words_<reg>.wav` medleys (all gestures of that group in a row), `variety_hello.wav` (one gesture five times, to hear the per-call jitter) and `babble_high.wav` (twelve random babbles, calm to lively). Listen with `afplay out/voice/words_high.wav`. The high-register medleys are also committed under `docs/voice/`.

`vocode.c` and `words.sh`: an earlier experiment, a 24-band channel vocoder over text-to-speech clips; kept for reference, not used.

`robot.c` and `samples.sh`: the sampled voice. `samples.sh [voice]` speaks a short set of words and interjections with macOS text-to-speech and renders each through several settings of `robot` (speed/pitch up by resampling, optional ring modulation, optional resonant peak, a high-pass for the speaker), one medley per setting under `docs/voice/sample_<voice>_<setting>.wav`, to choose the treatment by ear.
