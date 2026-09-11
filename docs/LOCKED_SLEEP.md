# Locked Sleep

Choose **Locked Sleep** near the bottom of Setup (open Setup with the PWR
button). The device briefly restarts and shows a small lock icon on black at
2% panel brightness. Pressing either button raises brightness to 22% for five
seconds after that press, then it returns to 2%.

Unlock with **five separate presses of BOOT, the second button, within five
seconds**. Release it between presses and after the fifth press. The device
restarts into its normal face. Holding a button never counts as repeated
presses; PWR never contributes to the unlock sequence.

The lock is stored in the `companion` NVS namespace as `locked`. Ordinary
power cycles, firmware resets and flashes that preserve NVS retain it. A
failed save leaves the menu open; a failed unlock save leaves the device
locked. Calibration and other settings keep their existing keys and values.
Erasing NVS/factory-resetting storage removes the lock along with settings.

## What runs while locked

The saved-lock check happens before the normal task initialization. There is
no microphone capture, listening or music detection, USB Music Lab capture,
speech/purr/effect playback, touch task, personality engine or render/push
worker. USB commands cannot enter capture while locked.

The amplifier is disabled and both audio codecs receive the shutdown
sequences from the pinned `esp_codec_dev` 1.5.11 driver. This also handles a
software reset, which can leave external chips powered. No I2S clocks start.
The touch controller is held in reset; the IMU is powered down after probe.

The panel retains one static image in its own GRAM. TE interrupts and the
fallback frame timer are disabled. BOOT is sampled every 20 ms with 40 ms
debouncing; PMIC key and USB status are checked every 100 ms. The maximum CPU
frequency lock is released, allowing 40 MHz idle operation and automatic
light sleep between checks on battery. With the USB console protection
configuration enabled, a connected USB cable keeps the no-light-sleep lock.

This is a low-activity mode, not ESP deep sleep or power-off: keeping a
visible icon needs the AMOLED powered. Actual current and battery duration
require hardware measurements.

Ordinary full sleep also inhibits speech/effects before stopping audio and
keeps them inhibited until waking. Its existing microphone shutdown disables
the codecs and I2S; the dimmed, animated drowsy state remains a separate mode.

## Verification

Host checks:

```sh
cd tools/host
./build.sh lock_test -Wextra -Werror -fsanitize=undefined
./bin/lock_test
./build.sh ui_harness
./bin/ui_harness
```

These check rolling five-second timing, the exact deadline, rejected contact
bounce, held buttons (including held at entry), PWR isolation, brightness
expiry, millisecond-clock wrap, and both menu actions. The same icon painter
exports `out/locked_sleep.ppm`; the menu exports `out/ui_locked_sleep.ppm`.
Both previews were visually reviewed. ESP-IDF 5.5.5 build passes.
AddressSanitizer could not initialize on this host (runtime deadlock before
`main`); the completed policy run used UndefinedBehaviorSanitizer.

This feature has not yet been flashed or tested on the device. Hardware
acceptance: enter from Setup; check both brightness buttons; try touch,
shaking, loud music and USB capture commands; verify four presses and slow
presses stay locked; unlock with five quick presses; check lock retention
after a PWR power cycle; measure idle current without USB. Verify normal
touch, sound, microphone capture and sleep resume after unlocking.

BOOT is GPIO0, active low, per the [official 1.75C schematic (KEYS)](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75C/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf).
It is also the hardware boot strap: holding BOOT during a hardware reset can
enter the ROM downloader before application firmware runs. The saved lock
is retained; release BOOT and restart to return to the lock screen.
