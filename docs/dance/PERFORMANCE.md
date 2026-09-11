# Dance rendering performance

Measured on the connected Waveshare ESP32-S3, USB-powered with the charging rim visible, microphone analysis running, production 30-second active / 1800-second dim timeout. The benchmark feeds synthetic 150 BPM features to animation only; the real microphone continues running.

## Verified hardware comparison

Eight six-second cases. Values are median one-second FPS reports, excluding each case’s first and last report to reduce transition contamination. This is a short stress comparison, not a guaranteed minimum or a worst-case execution-time proof.

| Scene | Before | Final build `b7fdf1d` |
|---|---:|---:|
| Spectrum only | 39 | 48.5 |
| Spectrum + row lasers | 22 | 42 |
| Spectrum + row spotlights | 22 | 40 |
| Spectrum + both, row | 17.5 | 33.5 |
| Spectrum + both, rim | 14 | 31.5 |
| Disco + both, rim | 13.5 | 25.5 |
| Plain shaded eyes + both, rim | 12 | 21.5 |
| Spectrum + both, rim, 33-degree tilt | 12 | 22 |

USB recovered after reconnection. On September 8, firmware `b7fdf1d` built, flashed with verified hashes, and booted with its commit ID confirmed in the device log. This includes the final sparse ring-coverage cache and restoration of normal four-sample eye coverage after dance. Boot confirmed the production 30-second active and 1800-second dim timeouts.

The final benchmark completed all eight scenes and restored the ordinary animation scheduler. It retained 11,772 bytes of render-task stack headroom; sampled audio-analysis reports ranged from 0.335–0.648 ms. No panic, watchdog or dropped-rectangle warning appeared. These are telemetry samples, not an audio WCET bound. One-second FPS reports still dipped to 16 FPS in the heaviest plain/tilted eye scenes, and the largest reported raster time was 60.8 ms. Large rotating shaded eyes, PSRAM copies and near-full-screen redraws still limit these cases; 60 FPS is not promised. Procedural pose/style variation and the live microphone make the table a workload comparison rather than a pixel-identical replay.

A further normal-music observation after the benchmark showed stable 11,772-byte stack headroom, no reset or dropped-rectangle warning, and full brightness while music remained admitted. Audio telemetry in that observation ranged from 0.322–0.686 ms. This is a short runtime check, not a long-duration soak or a physical 30-minute sleep/wake validation.

## Changes

- Spectrum targets are sampled at most every 40 ms (25 Hz), with height smoothing on every animation update. This does not slow ADC sampling, microphone analysis, music admission or beat detection.
- Lighting snapshots are rebuilt at most every 50 ms (20 Hz), checked by elapsed time rather than bucket crossings. Full switch-off bypasses the cap to erase old beams immediately. Motion/fades still use elapsed time. At lower display FPS the effective snapshot rate can be lower.
- Upright fully opaque spectrum bars fill constant-colour runs. The fade now reaches exactly full opacity, enabling the fast path instead of blending two shaders indefinitely.
- Lit dance eyes and spotlight geometry use two vertical coverage samples instead of four. Normal expressions retain four. Eye interiors and silhouettes remain opaque; only edge antialiasing changes. A tilted comparison changed 281 / 217,156 pixels.
- Charging-ring sector boundaries are calculated once per draw. The final sparse coverage table occupies 22,894 bytes of flash, no persistent RAM; hot ring code resides in IRAM. Generic ring geometry remains available for other radii/positions.
- Laser shows remain 30–60 seconds, with 30–50 seconds between shows. Spotlights run 18–36 seconds with 45–75 seconds between shows. Independent timers, RNGs, fades and row/rim layouts remain. Entry staggers their first appearances.

Across 32 seeds, ten minutes each: neither 30.3%, lasers alone 37.7%, spots alone 15.0%, together 17.0%. These are simulation observations including fades above 15% brightness, not hard quotas.

## Reproduce and validate

Send `MC_BENCH` followed by a newline over `/dev/cu.usbmodem101` at 115200 baud while awake. Close Music Lab/other serial readers first. The diagnostic automatically ends after 48 seconds and also cancels after a tap or entering settings. It resets the normal animation schedules on exit; no flash/NVS writes and no shortened power timeouts. It suppresses personality cameos during measurement. `DANCE_BENCH` identifies cases; `DANCE_COST` reports local paint, worker wait, PSRAM copy and accessory cost. Detailed timers run only during the diagnostic.

Passed: `dance_test`, `bar_runs_test`, `charge_cache_test`, `dance_quality`, `glass_test`, and `sweep`, built with undefined-behavior checking. The sweep exercised 49,932 frames with zero bounding-box/guard violations. Cache tests compare generic versus cached pixels and tiled output; bar tests compare 15,000 fractional/clipped rows; dance tests exercise both quality settings and old/new damage cleanup.

AddressSanitizer on this Mac deadlocked inside its own shadow-memory initialization before the tests entered `main`; those processes were stopped. No ASan pass is claimed. Build succeeds with the existing stack-frame size checks. The benchmark used the original 1 MiB application partition (21% free); the subsequent custom layout expands it to 4 MiB, leaving about 80% free without changing renderer RAM usage.
