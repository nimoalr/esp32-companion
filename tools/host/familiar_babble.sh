#!/bin/zsh
# Babble/reaction performances through the selected Familiar treatment.
# No firmware mutations. Python standard library only; no phoneme/TUNE dependency.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out/familiar_babble"
DEST="$ROOT/docs/voice"
mkdir -p "$OUT" "$DEST"
"$HERE/build.sh" robot

# Whole phrases preserve Junior's transitions and natural sentence contours.
# Each family has three performances; the two real-word anchors are shared.
NAMES=(hello whatever a_idle a_curious a_happy b_idle b_curious b_happy
       c_idle c_curious c_happy curious delight surprise annoyed laugh sleepy hum)
TEXTS=("hello" "whatever" "ooh, mm hmm" "hmm, ooh?" "ooh, wee!"
       "bip boop" "boo bee?" "bip, bip, wheee!"
       "doodle bee dah" "wibble dibble?" "bada bee, bada boo!"
       "hmm?" "ooh!" "aah!" "ugh" "ha ha ha" "aah, hmm" "hmm")
printf '' > "$OUT/list.tsv"
for ((i=1; i<=${#NAMES}; i++)); do
  name=${NAMES[$i]}
  printf '%s\t%s\n' "$name" "${TEXTS[$i]}" >> "$OUT/list.tsv"
  say -v Junior -r 165 -o "$OUT/$name.aiff" "[[pmod 60]] ${TEXTS[$i]}"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$OUT/$name.aiff" "$OUT/${name}_dry.wav"
  "$HERE/bin/robot" "$OUT/${name}_dry.wav" "$OUT/${name}_processed.wav" 1.55 0 0 0
  print "Rendered $name: ${TEXTS[$i]}"
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

def read(path):
    with wave.open(str(path), 'rb') as w:
        assert (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (rate, 1, 2)
        pcm = array.array('h', w.readframes(w.getnframes()))
    if sys.byteorder != 'little':
        pcm.byteswap()
    assert pcm and 100 < max(map(abs, pcm)) < 32767, path
    return list(pcm)

def rms(pcm):
    return math.sqrt(sum(v*v for v in pcm)/len(pcm))

def finish(pcm, target=0.12, fade=32):
    pcm = list(pcm)
    for i in range(fade):
        pcm[i] *= i/fade
        pcm[-i-1] *= i/fade
    gain = min(target*32767/rms(pcm), 0.7*32767/max(map(abs, pcm)))
    pcm = [round(v*gain) for v in pcm]
    if len(pcm) % 2:
        pcm.append(0)
    assert max(map(abs, pcm)) <= 22938 and pcm[0] == pcm[-1] == 0
    return pcm

def trim(pcm):
    threshold = max(map(abs, pcm))*10**(-50/20)
    active = [i for i, v in enumerate(pcm) if abs(v) >= threshold]
    assert active
    pcm = pcm[max(0, active[0]-128):min(len(pcm), active[-1]+321)]
    assert 800 < len(pcm) < rate*4
    return finish(pcm)

def write(path, pcm):
    data = array.array('h', pcm)
    if sys.byteorder != 'little':
        data.byteswap()
    with wave.open(str(path), 'wb') as w:
        w.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        w.writeframes(data.tobytes())

prompts = dict(line.split('\t') for line in (source/'list.tsv').read_text().splitlines())
clips = {name: trim(read(source/f'{name}_processed.wav')) for name in prompts}

# A purr attempt from Junior's voiced hum: choose a loud central 100 ms grain,
# repeat by overlap-add, then add a mild 24 Hz amplitude flutter. All timbre
# comes from the processed hum. The Hann windows sum to one at 50% overlap.
# This is a finite audition, not a firmware loop with ADPCM restart metadata.
hum = clips['hum']
grain_size = 1600
assert len(hum) >= grain_size + 320
starts = range(160, len(hum)-grain_size-159, 80)
start = max(starts, key=lambda i: sum(v*v for v in hum[i:i+grain_size]))
grain = hum[start:start+grain_size]
window = [0.5-0.5*math.cos(2*math.pi*i/grain_size) for i in range(grain_size)]
length, hop = rate*2, grain_size//2
purr = [0.0]*(length+grain_size)
for offset in range(0, len(purr)-grain_size+1, hop):
    for i, v in enumerate(grain):
        purr[offset+i] += v*window[i]
purr = purr[hop:hop+length]
purr = [v*(0.82+0.18*math.cos(2*math.pi*24*i/rate)) for i,v in enumerate(purr)]
clips['purr'] = finish(purr, target=0.075, fade=800)
prompts['purr'] = 'looped voiced grain from hmm; 24 Hz amplitude flutter'

groups = {
    'A_murmurs': ['hello', 'a_idle', 'a_curious', 'a_happy', 'whatever'],
    'B_bips': ['hello', 'b_idle', 'b_curious', 'b_happy', 'whatever'],
    'C_chatter': ['hello', 'c_idle', 'c_curious', 'c_happy', 'whatever'],
    'reactions': ['hello', 'curious', 'delight', 'surprise', 'annoyed', 'laugh', 'sleepy', 'purr', 'whatever'],
}
manifest = dict(sample_rate=rate, channels=1, bits=16, voice='Junior', rate=165,
                markup='[[pmod 60]]', treatment='robot speed=1.55 ring=0 depth=0 peak=0; 320 Hz high-pass',
                macos=subprocess.check_output(['sw_vers','-productVersion'], text=True).strip(),
                gap_ms=450, purr_grain_start=start, purr_grain_samples=grain_size,
                purr_hop_samples=hop, groups={})
finished = source/'finished'
finished.mkdir(exist_ok=True)
for name, pcm in clips.items():
    write(finished/f'{name}.wav', pcm)
for group, names in groups.items():
    medley, entries = [], []
    for name in names:
        if medley:
            medley.extend([0]*7200)
        pcm = clips[name]
        entries.append(dict(name=name, prompt=prompts[name], start_s=round(len(medley)/rate,3),
                            duration_s=round(len(pcm)/rate,3), samples=len(pcm), adpcm_bytes=len(pcm)//2,
                            peak_dbfs=round(20*math.log10(max(map(abs,pcm))/32768),2),
                            rms_dbfs=round(20*math.log10(rms(pcm)/32768),2)))
        medley.extend(pcm)
    path = dest/f'familiar_{group}.wav'
    write(path, medley)
    manifest['groups'][group] = dict(file=path.name, samples=len(medley),
                                    sha256=hashlib.sha256(path.read_bytes()).hexdigest(), clips=entries)
    print(f'{path.name}: {len(medley)/rate:.2f}s, {path.stat().st_size/1024:.0f} KiB')
(dest/'familiar_babble.json').write_text(json.dumps(manifest, indent=2)+'\n')
PY
