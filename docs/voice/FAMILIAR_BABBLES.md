# Familiar: babble and reactions

The owner chose **Familiar** (creature round 1 A, originally sample Junior B).
The sound treatment is now fixed: Junior at 165 words/minute with `[[pmod 60]]`,
resample ×1.55, the existing 320 Hz high-pass, no ring modulation or resonant
peak. The firmware's existing 54 words already use this treatment.

To give babble the same voice, Junior performs the nonsense itself. Each babble
is rendered as a whole phrase, preserving the voice's consonants, vowels and
transitions. The choices below concern vocabulary and delivery, not effects.
They share the same real-word anchors so the owner can assess consistency.
Same source and processing provide a common timbre; whether the performances
sound like one creature is a listening decision.

## Babble audition

| File | Length | Clip order |
| --- | ---: | --- |
| [A — Murmurs](familiar_A_murmurs.wav) | 4.61 s | hello → ooh, mm hmm → hmm, ooh? → ooh, wee! → whatever |
| [B — Bips](familiar_B_bips.wav) | 4.38 s | hello → bip boop → boo bee? → bip, bip, wheee! → whatever |
| [C — Chatter](familiar_C_chatter.wav) | 4.94 s | hello → doodle bee dah → wibble dibble? → bada bee, bada boo! → whatever |

Each has an idle, curious, and happy attempt between the word anchors. The
labels describe intent, not verified emotion. Choose the babble family that
fits, or reject the round; individual phrases can be mixed later.

## Reactions and purr audition

[familiar_reactions.wav](familiar_reactions.wav), 8.57 seconds:

| Start | Clip | Intended use |
| ---: | --- | --- |
| 0.00 s | hello | word reference |
| 0.80 s | hmm? | curiosity |
| 1.57 s | ooh! | delight |
| 2.28 s | aah! | surprise |
| 3.01 s | ugh | annoyance |
| 3.74 s | ha ha ha | laugh attempt |
| 4.59 s | aah, hmm | sleepy reaction attempt |
| 5.67 s | hum-derived purr, two seconds | touch response attempt |
| 8.12 s | whatever | word reference |

The purr uses a 100 ms voiced grain from Junior's processed “hmm”, repeated
with overlapping Hann windows, then gentle 24 Hz amplitude flutter and 50 ms
attack/release. Its audible material is the same voice. This is an audition of
the texture; it may sound too much like a repeating hum, which needs a human
listening decision. It is not yet an exported seamless firmware loop.

For the eventual touch purr, prepare a short loop plus onset/release, encode
the loop with its own reset state, and verify the decoded loop seam. Touch
duration would govern repetition and volume. A 0.4-second ADPCM loop is 3.2 KB;
the two-second preview is deliberately finite and should not be used as the
final loop definition.

## Production plan

After the babble style is selected, author about 24 whole babbles covering
quiet, curious, and lively deliveries; keep the good reactions and develop the
missing moods through the same voice. Favor short phrases and two/three-syllable
reactions. A bank averaging 0.65 seconds per babble takes 124,800 ADPCM bytes.
That is an estimate, not the size of a generated production bank.

Pick among clips by mood/energy and avoid the immediately previous clip.
Initially keep playback at exactly 16 kHz. Optional rate jitter of about ±3%
can add variety later, but changes both pitch and timing and needs resampling
within the fixed-rate shared I2S bus. Do not change the I2S clock to implement it.
Nothing heavy runs on the ESP32; use its existing ADPCM player.

Keep existing word IDs stable and append accepted babble/reaction IDs to the
existing `clip_t` table (`name`, even sample count, raw ADPCM data). The
[codec contract](CREATURE_ROUND1.md#playbackencoding-handoff) still applies.
The current procedural babble/purr remain in firmware until the replacements
are judged; no player or personality changes are part of this audition.

## Reproduce and verify

```sh
tools/host/familiar_babble.sh
```

Requires macOS `say` with Junior, `afconvert`, gcc, and Python's standard
library. The script builds `robot` with the standard host build script. It
writes four small 16 kHz mono 16-bit PCM WAVs plus
[a timing/level manifest](familiar_babble.json). Individual finished clips stay
in ignored `tools/host/out/familiar_babble/finished/`.

Speech edges are trimmed at −50 dB relative to peak, retaining 8 ms before and
20 ms after. Each speech clip is normalized to −18.42 dBFS RMS, subject to
−3.1 dBFS peak headroom; the purr is quieter at −22.5 dBFS RMS. The medleys use
450 ms separators and no trailing gap. These are PCM auditions, not decoded
ADPCM auditions; accepted production clips still need a codec round-trip check.
The producing agent cannot listen; numerical checks cannot establish quality.

Apple's archived documentation describes
[phoneme and TUNE input](https://developer.apple.com/library/archive/documentation/UserExperience/Conceptual/SpeechSynthesisProgrammingGuide/FineTuning/FineTuning.html)
for controlling syllables, pitch and duration. This machine's Junior did not
produce the requested result in probes: nominal 300/900 ms single-vowel TUNE
inputs produced about six seconds of active audio, and a short PHON input was
longer than three seconds. Those paths are not used. The committed auditions
use ordinary text spellings, whose pronunciation can depend on TTS version.

From Windows, run these separately after pulling:

```powershell
git pull
Start-Process .\docs\voice\familiar_A_murmurs.wav
Start-Process .\docs\voice\familiar_B_bips.wav
Start-Process .\docs\voice\familiar_C_chatter.wav
Start-Process .\docs\voice\familiar_reactions.wav
```
