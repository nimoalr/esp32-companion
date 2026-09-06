#!/bin/zsh
# The word clips for the firmware: text-to-speech -> the chosen treatment (robot) -> IMA ADPCM tables.
#   tools/host/clips.sh [voice=Junior] [speed=1.55]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
VOICE=${1:-Junior}
SPEED=${2:-1.55}
OUT="$HERE/out/clips/$VOICE"
mkdir -p "$OUT"
gcc -O2 -Wall -o "$HERE/bin/robot" "$HERE/robot.c" -lm
gcc -O2 -Wall -I"$HERE/../../main" -o "$HERE/bin/clips" "$HERE/clips.c" "$HERE/../../main/adpcm.c" -lm
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
  say -v "$VOICE" -r 165 -o "$OUT/$n.aiff" "[[pmod 60]] $w"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$OUT/$n.aiff" "$OUT/${n}_${slug}_dry.wav"
  "$HERE/bin/robot" "$OUT/${n}_${slug}_dry.wav" "$OUT/${n}_${slug}.wav" "$SPEED" 0 0 0
  echo "$n $w" >> "$OUT/list.txt"
  i=$((i+1))
done
cd "$HERE" && ./bin/clips "$OUT"
