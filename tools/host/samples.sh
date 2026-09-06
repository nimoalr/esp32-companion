#!/bin/zsh
# Candidate treatments for the sampled voice: a short set of words and interjections from one
# text-to-speech voice, each rendered through several settings of `robot`, one medley per
# setting under docs/voice/ so the treatment can be chosen by ear.
#   tools/host/samples.sh [voice=Junior]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
VOICE=${1:-Junior}
OUT="$HERE/out/samples/$VOICE"
mkdir -p "$OUT" "$ROOT/docs/voice"
gcc -O2 -Wall -o "$HERE/bin/robot" "$HERE/robot.c" -lm
CLIPS=(
  "hello" "uh oh" "wow" "really?" "whatever" "fuck you" "good night" "nice try"
  "hmm?" "ooh!" "aah" "ha ha ha" "hm hm" "eeeh" "ugh" "wheee"
)
i=0
: > "$OUT/list.txt"
for w in "${CLIPS[@]}"; do
  n=$(printf "%02d" $i)
  say -v "$VOICE" -r 165 -o "$OUT/$n.aiff" "[[pmod 60]] $w"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$OUT/$n.aiff" "$OUT/${n}_dry.wav"
  echo "$n $w" >> "$OUT/list.txt"
  i=$((i+1))
done
# name speed ring_hz ring_depth peak_hz
VARIANTS=(
  "A_pitched 1.30 0 0 0"
  "B_pitched_more 1.55 0 0 0"
  "C_metallic 1.30 45 0.45 0"
  "D_metallic_bright 1.40 60 0.35 2200"
)
for v in "${VARIANTS[@]}"; do
  set -- ${=v}
  name=$1; speed=$2; ring=$3; depth=$4; peak=$5
  mkdir -p "$OUT/$name"
  for f in "$OUT"/*_dry.wav; do
    b=$(basename "$f" _dry.wav)
    "$HERE/bin/robot" "$f" "$OUT/$name/$b.wav" "$speed" "$ring" "$depth" "$peak"
  done
  python3 - "$OUT/$name" "$ROOT/docs/voice/sample_${VOICE}_${name}.wav" <<'PY'
import sys, wave, glob, os
d, dest = sys.argv[1], sys.argv[2]
sil = b"\x00\x00" * (16000 * 45 // 100)
with wave.open(dest, "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    for f in sorted(glob.glob(os.path.join(d, "*.wav"))):
        with wave.open(f, "rb") as r: w.writeframes(r.readframes(r.getnframes()))
        w.writeframes(sil)
print("wrote", dest)
PY
done
