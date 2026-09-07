#!/bin/sh
set -eu
cd "$(dirname "$0")"
echo 'Music Lab: http://localhost:8765 (Chrome or Edge)'
exec python3 -m http.server 8765 --bind 127.0.0.1
