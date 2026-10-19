#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Emulador FPGA 6502 - lanzador CON depuracion HTTP
#
#  Igual que run.sh, pero activa el servidor HTTP para poder ver la pantalla y
#  el estado del emulador desde el navegador o desde un agente de IA.
#
#  Uso:
#    ./run_debug.sh                       -> UART 4000, debug en 8080
#    ./run_debug.sh 4001                  -> otro puerto de UART
#    ./run_debug.sh 4000 otra.img         -> otra imagen de SD
#    ./run_debug.sh 4000 tarjeta.img 9000 -> debug en el puerto 9000
#
#  Variables de entorno:
#    EMU_AUDIO_DEVICE=...  -> forzar un dispositivo de audio por nombre
#    EMU_WAV=grabacion.wav -> grabar la salida del SID a WAV
#    EMU_TRACE=1           -> mostrar diagnostico de rendimiento y audio
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")"

PORT="${1:-4000}"
IMAGE="${2:-tarjeta.img}"
DEBUG_PORT="${3:-8080}"

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

echo "============================================================"
echo "  Emulador FPGA 6502   [DEPURACION HTTP ACTIVA]"
echo "  ROM    : rom/monitor.bin"
echo "  SD     : ${IMAGE}"
echo "  Puerto : ${PORT}   (PuTTY: Raw, localhost:${PORT})"
echo "  Debug  : http://localhost:${DEBUG_PORT}"
echo "           /            vista previa + estado"
echo "           /screen.png  captura de lo que saldria por HDMI"
echo "           /vram.json   tilemap, atributos y sprites"
echo "           /state.txt   GPIO, SID, video, UART"
echo "============================================================"

./emulador.exe rom/monitor.bin "${PORT}" "${IMAGE}" "--debug=${DEBUG_PORT}"
