#!/usr/bin/env python3
"""Extrae el contenido binario de una ROM VHDL generada (rom.vhd).

Los archivos rom.vhd / "rom - monitor.vhd" del proyecto fpga-6502-16k contienen
líneas del tipo:

    when x"0000" => data_out<= x"A2";

Este script parsea esas líneas y produce el .bin de 16 KB correspondiente.
Uso:  python extract_rom.py <rom.vhd> <salida.bin>
"""
import sys
import re

def main(vhd_path, out_path):
    pattern = re.compile(r'when\s+x"([0-9A-Fa-f]{4})"\s*=>\s*data_out<=\s*x"([0-9A-Fa-f]{2})"')
    data = bytearray(16384)
    found = 0
    with open(vhd_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                addr = int(m.group(1), 16)
                val = int(m.group(2), 16)
                if addr < 16384:
                    data[addr] = val
                    found += 1
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"OK: {out_path} -> {found} registros, {len(data)} bytes")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
