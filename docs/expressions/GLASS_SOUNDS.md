# Recorded glass foley

[Listen to the exact decoded firmware effects](../voice/glass_foley.wav) (7 seconds, 16 kHz mono).

| Time | Sound |
| --- | --- |
| 0.15 s | Glass-screen bonk |
| 1.20 s | Short crack extension |
| 2.20 s | Final shatter |
| 3.70 s | Falling debris |
| 4.50 s | Bonk + crack together |
| 5.50 s | Bonk + final shatter together |

The [animation preview](refinements.mp4) also has the bonks and crack extensions synchronized to its four scripted hits. These are host renders, not a microphone recording of the speaker. I cannot listen to the candidates; source identity, waveform measurements and playback tests inform this first treatment. Speaker character and balance still need your judgment.

## Sources and permission

- [Glass Knock by IMALUIGI](https://freesound.org/people/IMALUIGI/sounds/367704/), Freesound, November 7, 2016. The author describes recording knocks on a glass computer screen. Listed **Creative Commons 0**. The bundled `knock_excerpt.wav` is 6.155–6.545 seconds from the author's public high-quality MP3 preview, converted to 16 kHz mono. [Preview source](https://cdn.freesound.org/previews/367/367704_2379144-hq.mp3).
- [Glass Break by Till Behrend, uploaded by TinyWorlds](https://opengameart.org/content/glass-break), OpenGameArt, January 28, 2015. Listed **CC0**, with the uploader stating the author's permission. `break_excerpt.wav` is the first 1.08 seconds of `glass_breaking.wav`, converted to 16 kHz mono. [Original download](https://opengameart.org/sites/default/files/glass_breaking.wav).

Source pages and licensing were checked September 8, 2026. [CC0 dedication](https://creativecommons.org/publicdomain/zero/1.0/). No paid library or attribution-only/noncommercial sample is included.

## Treatment and playback

Both sources receive a 380 Hz high-pass, 6.8 kHz low-pass and broad +4 dB presence boost around 1.5 kHz. Short fades, level normalization and soft limiting increase useful body while leaving peak headroom. Crack and debris effects are different excerpts of the same break, so the gag shares one material character. IMA ADPCM storage totals **16,400 bytes** (0.39 s bonk, 0.28 s crack, 1.08 s shatter, 0.30 s debris). Three mixer voices allow bonk/crack overlap. Small collisions and reel ticks retain their procedural sounds.

The old effects peaked around 2,200 PCM units before their envelopes and could lose their tails when the amplifier shut off. Recorded effects now peak around 21,000–22,000 before the event gain, with RMS around 3,800–5,100. The global user volume is still respected. The audio bus explicitly has six 240-frame DMA buffers (90 ms at 16 kHz); speech/effects flush 120 ms of quiet before switching the amplifier off. New effects arriving during that tail are mixed and drained as well.

Regenerate from the bundled short CC0 source excerpts, without a network request:

```sh
tools/host/glass_sounds.sh
```

The script uses ffmpeg and the plain-C firmware ADPCM encoder; it writes `main/sfx_samples.h` and the WAV audition. No new codec or DSP library runs on the ESP32.
