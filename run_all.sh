#!/usr/bin/env bash
# On the machine the ESP32 is plugged into (repo root): build + flash the firmware, then the full measurement run. Holds the shared board lock for both.
#   ./run_all.sh            full run, raw CSVs into results/raw
#   ./run_all.sh --quick    short run into results/raw (use OUT=... to keep it elsewhere)
set -euo pipefail
cd "$(dirname "$0")"
LOCK="${LOCK:-/usr/local/share/esp32.lock}"; PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"; PORT="${PORT:-/dev/ttyUSB0}"
OUT="${OUT:-results/raw}"; mkdir -p logs "$OUT"
exec 9>>"$LOCK"
flock -n 9 || { echo "board busy, waiting..."; flock 9; }
(cd firmware && "$PIO" run -t upload --upload-port "$PORT") 2>&1 | tail -4
python3 host/run_bench.py --out "$OUT" "$@" 2>&1 | tee logs/run_bench.log
