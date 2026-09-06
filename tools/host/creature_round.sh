#!/bin/zsh
# One creature, round 1: identical Junior takes for words and interjections.
# Run on macOS: tools/host/creature_round.sh
# Requires say/Junior, afconvert, ffmpeg with rubberband, gcc, Python stdlib only.
# Writes audition WAVs + a timing/level manifest under docs/voice/. No firmware edits.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out/creature_round1"
DEST="$ROOT/docs/voice"
for dependency in say afconvert ffmpeg python3; do
  command -v "$dependency" >/dev/null || { print -u2 "Missing $dependency"; exit 1; }
done
filters=$(ffmpeg -hide_banner -filters 2>/dev/null)
[[ "$filters" == *rubberband* ]] || { print -u2 'ffmpeg needs the rubberband filter'; exit 1; }
mkdir -p "$OUT" "$DEST"
"$HERE/build.sh" robot

# Punctuation shapes the source; no extracted pitch track or oscillator carrier.
CLIPS=("hello" "ooh!" "uh oh" "aah!" "seriously?" "hmm?"
       "whatever" "ugh" "fuck you" "ha ha ha" "nice try" "wheee!")
NAMES=(hello delight uh_oh surprise seriously curious
       whatever disgust fuck_you laugh nice_try celebration)
: > "$OUT/list.tsv"
for ((i=1; i<=${#CLIPS}; i++)); do
  n=$(printf '%02d' $i)
  printf '%s\t%s\t%s\n' "$n" "${NAMES[$i]}" "${CLIPS[$i]}" >> "$OUT/list.tsv"
  say -v Junior -r 165 -o "$OUT/$n.aiff" "[[pmod 60]] ${CLIPS[$i]}"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$OUT/$n.aiff" "$OUT/${n}_dry.wav"

  # A is the owner's previous B: resample x1.55, 320 Hz HP, no ring/peak.
  "$HERE/bin/robot" "$OUT/${n}_dry.wav" "$OUT/${n}_A.wav" 1.55 0 0 0

  # Work at 48 kHz for the host effects, with float intermediates/headroom.
  # Pad the tail to let the time stretcher flush; the assembly step trims it.
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$OUT/${n}_dry.wav" \
    -af 'volume=0.5,aresample=48000,apad=pad_dur=0.25,rubberband=pitch=2.6:tempo=1.35:formant=preserved:pitchq=quality:transients=mixed' \
    -c:a pcm_f32le "$OUT/${n}_shift.wav"
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$OUT/${n}_shift.wav" \
    -af 'highpass=f=400:p=2,lowpass=f=7000:p=2' \
    -ar 16000 -ac 1 -c:a pcm_s16le "$OUT/${n}_B.wav"

  # C adds sidebands and a short moving comb to B's voice; no separate carrier voice.
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$OUT/${n}_shift.wav" \
    -af "aeval=val(0)*(0.78+0.22*sin(2*PI*90*t)),flanger=delay=0.7:depth=0.5:regen=10:width=28:speed=0.8:interp=quadratic,highpass=f=400:p=2,lowpass=f=7000:p=2" \
    -ar 16000 -ac 1 -c:a pcm_s16le "$OUT/${n}_C.wav"
  print "Rendered $n: ${CLIPS[$i]}"
done

python3 - "$OUT" "$DEST" <<'PY'
import array
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import wave

source, dest = map(Path, sys.argv[1:])
rate = 16000
variants = {'A': 'familiar', 'B': 'round', 'C': 'alloy'}
tracks = {v: [] for v in variants}
rows = {v: [] for v in variants}
gap = [0] * 5600  # 350 ms between clips, none after the last.

def read_trim(path):
    with wave.open(str(path), 'rb') as w:
        assert (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (rate, 1, 2), path
        pcm = array.array('h', w.readframes(w.getnframes()))
    if sys.byteorder != 'little':
        pcm.byteswap()
    assert pcm and max(map(abs, pcm)) < 32767, f'Empty or clipped input: {path}'
    # Relative -50 dB trim, plus 8/20 ms guards for quiet consonants and releases.
    threshold = max(1, max(map(abs, pcm)) * 10**(-50/20))
    active = [i for i, v in enumerate(pcm) if abs(v) >= threshold]
    assert active, f'Silent input: {path}'
    pcm = list(pcm[max(0, active[0]-128):min(len(pcm), active[-1]+321)])
    assert 800 <= len(pcm) <= rate * 5, f'Suspicious duration: {path}'
    # 2 ms edge ramps, outside the active speech where guards are available.
    for i in range(32):
        pcm[i] = round(pcm[i] * i / 32)
        pcm[-1-i] = round(pcm[-1-i] * i / 32)
    if len(pcm) % 2:
        pcm.append(0)  # Ready for the existing paired-sample ADPCM encoder.
    return pcm

def rms(pcm):
    return math.sqrt(sum(v*v for v in pcm) / len(pcm))

def write(path, pcm):
    data = array.array('h', pcm)
    if sys.byteorder != 'little':
        data.byteswap()
    with wave.open(str(path), 'wb') as w:
        w.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        w.writeframes(data.tobytes())

for line in (source / 'list.tsv').read_text().splitlines():
    number, name, spoken = line.split('\t')
    clips = {v: read_trim(source / f'{number}_{v}.wav') for v in variants}
    # Match RMS across the three versions of each utterance, with -3.1 dBFS
    # peak headroom and at most -18.4 dBFS RMS. This is not perceptual LUFS matching.
    target = min(0.12 * 32767, *(rms(p) * (0.7*32767 / max(map(abs, p))) for p in clips.values()))
    for v, pcm in clips.items():
        gain = target / rms(pcm)
        pcm = [round(x * gain) for x in pcm]
        assert max(map(abs, pcm)) <= 22938
        if tracks[v]:
            tracks[v].extend(gap)
        start = len(tracks[v]) / rate
        tracks[v].extend(pcm)
        rows[v].append(dict(number=int(number), name=name, text=spoken,
                            start_s=round(start, 3), duration_s=round(len(pcm)/rate, 3),
                            samples=len(pcm), adpcm_bytes=len(pcm)//2,
                            peak_dbfs=round(20*math.log10(max(map(abs, pcm))/32768), 2),
                            rms_dbfs=round(20*math.log10(rms(pcm)/32768), 2)))
        # Keep individual finished takes in ignored output for the next round/encoder.
        folder = source / v
        folder.mkdir(exist_ok=True)
        write(folder / f'{number}_{name}.wav', pcm)

manifest = dict(sample_rate=rate, channels=1, bits=16, gap_ms=350,
                source_voice='Junior', source_rate=165, source_markup='[[pmod 60]]',
                macos=subprocess.check_output(['sw_vers', '-productVersion'], text=True).strip(),
                ffmpeg=subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0],
                treatments={
                    'A': 'robot speed=1.55, ring=0, peak=0, HP=320 Hz',
                    'B': 'rubberband pitch=2.6, tempo=1.35, formant=preserved; HP=400 Hz, LP=7000 Hz',
                    'C': 'B + 90 Hz modulation depth=0.22 + flanger delay=0.7ms depth=0.5ms regen=10% width=28% speed=0.8Hz'},
                variants={})
for v, name in variants.items():
    path = dest / f'creature_{v}_{name}.wav'
    write(path, tracks[v])
    manifest['variants'][v] = dict(file=path.name, samples=len(tracks[v]),
                                   sha256=hashlib.sha256(path.read_bytes()).hexdigest(), clips=rows[v])
    print(f'{path.name}: {len(tracks[v])/rate:.2f}s, {path.stat().st_size/1024:.0f} KiB')
(dest / 'creature_round1.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY
