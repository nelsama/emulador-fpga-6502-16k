#!/usr/bin/env bash
# Cosimulación de la CPU: programas aleatorios (todos los opcodes menos JAM) ejecutados en
# src/cpu_6502.cpp y en cpu65xx_fast.vhd (GHDL). Compara cada escritura y los ciclos.
# Uso: tools/cosim_cpu/run.sh [rondas]     VHDL=ruta/cpu65xx_fast.vhd para otro RTL.
set -e
AQUI="$(cd "$(dirname "$0")" && pwd)"
RAIZ="$(cd "$AQUI/../.." && pwd)"
VHDL="${VHDL:-$RAIZ/../fpga-6502-16k/src/6502/cpu65xx_fast.vhd}"
OPC="--std=08 -fsynopsys -frelaxed"
D="$RAIZ/.tmp/cosim"
mkdir -p "$D/inc"
# Copia con un bus.h falso: 64 KB planos que registran las escrituras.
cp "$RAIZ"/src/cpu_6502.* "$RAIZ"/src/cpu_cycles.h "$RAIZ"/src/cpu_opcodes.h "$AQUI"/fake/bus.h "$D/inc/"
cd "$D"
g++ -O2 -std=c++17 -Iinc -o gen.exe "$AQUI/gen.cpp" inc/cpu_6502.cpp
ghdl -a $OPC "$VHDL"
ghdl -a $OPC "$AQUI/tb.vhd"
ghdl -e $OPC tb
for s in $(seq 1 "${1:-5}"); do
  ./gen.exe "$s"
  ghdl -r $OPC tb --ieee-asserts=disable
  perl "$AQUI/cmp.pl"
done
