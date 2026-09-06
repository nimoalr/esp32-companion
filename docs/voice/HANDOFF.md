# Voice handoff: brief for another agent

You are taking over the *voice* of a desk companion: a Waveshare ESP32-S3-Touch-AMOLED-1.75C
(ESP32-S3R8, 240 MHz dual core, 8 MB PSRAM, 16 MB flash) with a round AMOLED showing a pair of
animated eyes, two microphones (ES7210), and an ES8311 DAC into an NS4150B class-D amplifier
driving a speaker about two centimetres across. Firmware is ESP-IDF 5.5.5, plain C, in this
repository. The character is Anki-Vector-inspired: it emotes with its eyes, and it should vocalise
like Vector, wordless chirps for moods plus short *real words* with attitude ("hello", "uh-oh",
"seriously?", "whatever", "fuck you", "nice try"). The owner wants the words and the chirps to sound
like **one creature**, and everything must stay audible through that tiny speaker.

## Hard constraints

* The speaker carries nothing below about 400 Hz. Anything low must get its weight from timbre,
  not pitch. The procedural voice's fundamentals sit between 440 Hz and 1 kHz for that reason.
* Playback is 16 kHz mono 16-bit over I2S (the bus is shared with the microphones, full duplex).
* CPU budget on the board for sound: under 1.5 ms per 16 ms audio frame on core 0, next to the
  existing microphone analysis (~0.3 ms). Flash budget for clips: a few hundred KB is fine.
* No cloud, no licensed speech engine on the device. Anything heavy happens on the host, once.
* The repository's host tooling is plain C compiled with gcc (`tools/host/build.sh <name>`), a
  `zsh` script or two, macOS `say` + `afconvert` for text-to-speech, `ffmpeg` present, **no numpy**.
  The owner listens on Windows: results must be committed as WAV under `docs/voice/` so they can
  `git pull` and `Start-Process` the file. Keep files small (16 kHz mono); trim silence.
* The agent producing the sounds cannot listen to them. Design for that: produce a small number of
  clearly different candidates per round, name them, list the clip order, and ask for a pick.

## What exists (all on the branch, see `git log -- main/voice.c tools/host docs/voice`)

* `main/voice.c` / `voice.h`: the **procedural voice**, pure C, host-buildable. Two oscillators
  (fundamental with a little 2nd harmonic, plus a partner a fifth or octave up), utterances as
  sequences of syllables each with a pitch contour (start/middle/end in semitones from a register
  base), a vowel (two band-pass formant resonators over a harmonic-rich source, with diphthongs),
  an onset consonant hint (breath / click / pop / glide / liquid), vibrato, trill (also used as an
  amplitude flutter for the purr), darkened breath noise, attack/release, staccato pulses, a
  one-pole low-pass, a soft limiter, and per-call detune and duration jitter. Three registers
  (low 330 Hz, mid 440, high 587); **the owner chose high**. Seventeen mood gestures (happy, laugh,
  sad, surprised, scared, angry, annoyed, yawn, purr, hm, confused, dizzy, protest, knocked-out,
  oh, blip, wake) that the owner rates as **good** in the high register. A `voice_babble()` that
  composes random 1-4 syllable nonsense for idle chatter. Twenty-eight word-like gestures built
  from the same syllables (hello, uh-oh, wow, oh-no, okay, bye-bye, oopsie, really?, seriously?,
  whatever, no-way, thank-you, hooray, sorry, peekaboo, bingo, wakey-wakey, goodnight,
  good-morning, ooh-la-la, aha, come-on, excuse-me, how-rude, yummy, bravo, hi-there, oh-really?)
  which the owner finds **muddy**: the melody is right, the articulation is not.
* `tools/host/voice_render.c`: renders the vocabulary to WAV (`moods_<reg>.wav`,
  `words_<reg>.wav`, `babble_high.wav`, `variety_hello.wav`).
* `tools/host/vocode.c` + `words.sh`: a 24-band channel vocoder over macOS text-to-speech clips.
  Tried with a sawtooth carrier following the speech pitch (judged "muddy / two voices") and with
  the procedural voice's own tone as carrier at 587 Hz with intonation expanded (judged "like two
  voices mashed together"). Kept for reference, not used.
* A parametric resynthesis (speech -> 10 ms tracks of pitch, loudness, F1/F2 by LPC, voicing ->
  said by the procedural synth) was built, judged **"awful"**, and deleted (in git history,
  commit "spoken words as 10 ms parameter tracks").
* `tools/host/robot.c` + `samples.sh`: the **sample route**. Text-to-speech clips (voice
  "Junior", a child's voice; Samantha/Daniel were tried and rejected as too far from the chirps)
  through one treatment: resample for speed/pitch, optional ring modulation, optional resonant
  peak, a 320 Hz high-pass, normalisation. Four candidates are in `docs/voice/sample_Junior_*.wav`.
  **The owner picked B: `sample_Junior_B_pitched_more.wav`** (resample x1.55, no ring, no peak)
  as the interim word voice, alongside the procedural high register for moods. Nothing of this is
  on the board yet: the firmware plays no sound at all so far (the ES8311/amplifier path is not
  written).

## What the owner said, verbatim where it matters

* On the first procedural set: "the high is definitely the best but that's because it has the
  highest chance to reach through the speaker. but there's some amount of background white noise"
  (fixed: breath noise cut and darkened).
* "some of the mood sounds are way too low now if you look at the spectrum" (fixed: no contour
  below 4 semitones under the base).
* On procedural words: "hi is way too short, we prob want to avoid single syllables words since we
  can't articulate them easily"; "i want real words"; "we should have witty words tbh, it needs to
  have personality"; "more words like fuck you, a bit of soft insults"; "all of these words are
  great but they sound still quite muddy, i'd like to get closer to the voice of vocoder of anki
  vector".
* On the vocoded TTS: Junior closest; "the voice is very different from the overall sounds we
  generated; so the TTS output really needs to be closer in tone to the rest of the mood sounds
  and burble / chirps"; "pitch should definitely go higher"; "they're very flat in tone tbh, not a
  lot of intonations variations"; then "it's like you just mashed both voice on top of each other
  instead of taking the tts and reshaping it to sound closer to the procedural voice so everything
  is consistent".
* On the parametric resynthesis: "ew. this sounds awful".
* Vector for reference: Acapela text-to-speech pushed through Krotos Dehumaniser; sample-based
  vocalisations designed to match. The owner keeps pointing at Vector's vocoder sound.

## The open problem

One voice for both chirps and words, audible through the speaker, with real intonation and
personality. Directions not yet tried, in rough order of promise:

1. **Make the chirps from the same processed voice as the words** (Vector's way): text-to-speech
   or recorded interjections ("hmm?", "ooh", "aah", "ha ha", "ugh", a yawn) through the exact
   treatment the words get, so consistency is by construction; keep the procedural synth only for
   the babble and the touch-driven purr, or drop it. The owner has heard eight such interjections
   in the sample candidates but has not judged them separately.
2. **A better treatment than plain resampling**: pitch-shift with formant preservation (PSOLA or
   a phase vocoder on the host) so the voice goes up without the chipmunk squeeze, then a light
   Dehumaniser-style layer (subtle ring mod, a short comb/flanger, harmonic exciter) tuned by ear
   in a few named candidates.
3. **Intonation**: `say` accepts `[[pmod N]]` (pitch modulation range) and `[[pbas N]]` (base
   pitch); punctuation matters; several takes per word with different prosody give variety for
   free. Expanding the pitch contour after the fact (measure the speech pitch, resynthesise) is
   what produced the "mashed" result; prefer shaping the source.
4. **Variety on the board** without more flash: playback-rate jitter of a few percent per call,
   two or three takes per frequent word.
5. Any on-device speech engine (SAM, espeak-ng, Flite) was ruled out: worse than the above,
   licence or size problems.

Deliverables the owner expects from you: WAV candidates under `docs/voice/` with the clip order
listed, a pick-one question per round, then, once a treatment is chosen, the full word list plus
interjections rendered through it and encoded compactly for the firmware (IMA ADPCM is the
intended format, ~8 KB/s at 16 kHz), with the host script committed so the set can be regenerated.
The playback driver and the personality engine that triggers sounds are being written separately;
coordinate on the clip table format (`name`, sample count, ADPCM bytes).
