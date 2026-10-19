#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Emulador FPGA 6502 - lanzador
#
#  Uso:
#    ./run.sh                       -> monitor + tarjeta.img, UART en 4000
#    ./run.sh 4001                  -> otro puerto de UART
#    ./run.sh 4000 otra.img         -> otra imagen de SD
#
#  Para depurar (ver la pantalla y el estado por HTTP), usa ./run_debug.sh
#
#  Variables de entorno:
#    EMU_DEBUG_PORT=8080   -> igual que --debug
#    EMU_AUDIO_DEVICE=...  -> forzar un dispositivo de audio por nombre
#    EMU_WAV=grabacion.wav -> grabar la salida del SID a WAV
#    EMU_TRACE=1           -> mostrar diagnostico de rendimiento y audio
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")"

PORT="${1:-4000}"
IMAGE="${2:-tarjeta.img}"

# Paquetes de MSYS2 (el makefile los busca en el PATH).
export PATH="/ucrt64/bin:$PATH"

if [ ! -x emulador.exe ] || [ -n "$(find src -newer emulador.exe -name '*.cpp' -o -newer emulador.exe -name '*.h' 2>/dev/null | head -1)" ]; then
  echo "Compilando..."
  make -j4
fi

if [ ! -f rom/monitor.bin ]; then
  echo "[!] Falta rom/monitor.bin"
  exit 1
fi

# Argumentos extra (p. ej. --debug) a partir del tercero.
EXTRA=("${@:3}")

echo "============================================================"
echo "  Emulador FPGA 6502"
echo "  ROM    : rom/monitor.bin"
echo "  SD     : ${IMAGE}"
echo "  Puerto : ${PORT}   (PuTTY: Raw, localhost:${PORT})"
echo "  Debug  : no (usa ./run_debug.sh para activarlo)"
echo "============================================================"

./emulador.exe rom/monitor.bin "${PORT}" "${IMAGE}" "${EXTRA[@]}"
