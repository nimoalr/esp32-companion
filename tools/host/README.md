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

`vocode.c` and `words.sh`: the word clips. `words.sh [voice] [pitch_mult] [robot]` speaks the word list with macOS text-to-speech, converts to 16 kHz mono and runs each clip through `vocode` (a 24-band channel vocoder whose sawtooth carrier follows the speech's pitch, shifted by `pitch_mult`; noise where unvoiced; the speech's own sibilance added back), then writes `docs/voice/tts_words_<voice>_p<pitch>.wav` with all clips 400 ms apart. Clips and the numbered list land in `out/words/<voice>/`.
