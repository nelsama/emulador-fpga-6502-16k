#!/bin/bash
# Arranca el emulador y ejecuta una prueba de SAVE/LOAD con archivo multi-sector.
# Uso: tools/test_sd.sh [puerto]
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-4017}
mkdir -p .tmp
rm -f .tmp/t.img
./.tmp/emu rom/monitor.bin "$PORT" .tmp/t.img > .tmp/emu.log 2>&1 &
P=$!
sleep 3
python3 tools/probe_saveload.py "$PORT"
kill $P 2>/dev/null
