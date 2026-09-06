#!/bin/zsh
# Render the word clips: macOS text-to-speech -> 16 kHz mono WAV -> the channel vocoder,
# then a medley per voice under docs/voice/ for listening.
#   tools/host/words.sh [voice=Junior] [base_hz=587] [expand=2.0] [robot=0.85]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
VOICE=${1:-Junior}
BASE=${2:-587}
EXPAND=${3:-2.0}
ROBOT=${4:-0.85}
OUT="$HERE/out/words/$VOICE"
mkdir -p "$OUT"
gcc -O2 -Wall -o "$HERE/bin/vocode" "$HERE/vocode.c" -lm
WORDS=(
  "hello" "uh oh" "wow" "oh no" "okay" "bye bye" "oopsie" "really?" "seriously?" "whatever" "no way"
  "thank you" "hooray" "sorry" "peekaboo" "bingo" "wakey wakey" "good night" "good morning" "ooh la la"
  "aha" "come on" "excuse me" "how rude" "yummy" "bravo" "hi there" "oh really?"
  "fuck you" "shut up" "you idiot" "go away" "bite me" "nerd" "loser" "boring" "oh please" "dumb dumb"
  "silly" "buzz off" "get lost" "nope" "meh" "as if" "not my problem" "leave me alone" "nice try"
  "you wish" "talk to the hand" "whatever, human" "I am watching you" "do not touch me" "feed me" "I am bored"
)
i=0
: > "$OUT/list.txt"
for w in "${WORDS[@]}"; do
  n=$(printf "%02d" $i)
  slug=$(echo "$w" | tr -c 'a-zA-Z0-9\n' '_' | tr -s '_' | sed 's/_$//')
  say -v "$VOICE" -r 170 -o "$OUT/$n.aiff" "[[pmod 70]] $w"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$OUT/$n.aiff" "$OUT/${n}_${slug}_dry.wav"
  "$HERE/bin/vocode" "$OUT/${n}_${slug}_dry.wav" "$OUT/${n}_${slug}.wav" "$BASE" "$EXPAND" "$ROBOT" >/dev/null
  echo "$n $w" >> "$OUT/list.txt"
  i=$((i+1))
done
# medley: every clip with 400 ms of silence between
python3 - "$OUT" "$ROOT/docs/voice/tts_words_${VOICE}_${BASE}hz_x${EXPAND}.wav" <<'PY'
import sys, wave, glob, os
out_dir, dest = sys.argv[1], sys.argv[2]
files = sorted(f for f in glob.glob(os.path.join(out_dir, "*.wav")) if not f.endswith("_dry.wav") and "medley" not in f)
sil = b"\x00\x00" * (16000 * 4 // 10)
with wave.open(dest, "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    for f in files:
        with wave.open(f, "rb") as r:
            w.writeframes(r.readframes(r.getnframes()))
        w.writeframes(sil)
print("wrote", dest, len(files), "clips")
PY
