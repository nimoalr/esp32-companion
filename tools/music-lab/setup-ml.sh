#!/bin/sh
set -eu
MUSIC_LAB_ENV="${MUSIC_LAB_ENV:-$HOME/.cache/companion-music-lab/venv}"
python3.12 -m venv "$MUSIC_LAB_ENV"
"$MUSIC_LAB_ENV/bin/python" -m pip install 'audio-separator[cpu]==0.47.0' 'audioread==3.1.0'
printf 'Local stem analysis installed. Restart Music Lab after saving your recordings.\n'
