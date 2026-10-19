"""Genera src/video/font_c64.h a partir de font_data.vhd.

La fuente del Core de Vídeo es el charset del C64 (8x8, 1bpp), remapeado a
ASCII y colocado en los tiles 0..95 (códigos $20..$7F).
"""

import re

VHD = r"D:\Proyectos\fpga-6502-16k\src\hdmi\font_data.vhd"
OUT = r"D:\Proyectos\EmuladorFpga6502\src\video\font_c64.h"

txt = open(VHD, encoding="utf-8", errors="replace").read()

# Entradas del tipo:  N => "b" & x"XX",
entries = {}
pat = re.compile(r'(\d+)\s*=>\s*"([01])"\s*&\s*x"([0-9A-Fa-f]{2})"')
for m in pat.finditer(txt):
    idx = int(m.group(1))
    entries[idx] = (int(m.group(2)), int(m.group(3), 16))

if not entries:
    raise SystemExit("No se encontraron entradas en font_data.vhd")

n = max(entries) + 1
rows = []
for i in range(768):          # 96 caracteres x 8 filas
    bit, val = entries.get(i, (0, 0x00))
    rows.append(val | (bit << 8))

lines = []
lines.append("// Generado por tools/gen_font.py desde fpga-6502-16k/src/hdmi/font_data.vhd")
lines.append("// Fuente 8x8 del C64 (1bpp), 96 caracteres ASCII desde $20.")
lines.append("// Cada entrada es 1 fila de 8 pixeles en el bit alto de la palabra.")
lines.append("#pragma once")
lines.append("#include <cstdint>")
lines.append("")
lines.append("static const uint8_t kFontC64[96 * 8] = {")
for c in range(96):
    fila = ", ".join(f"0x{rows[c * 8 + r] & 0xFF:02X}" for r in range(8))
    ch = chr(0x20 + c)
    disp = ch if ch.isprintable() else "?"
    lines.append(f"    {fila},  // '{disp}' (${0x20 + c:02X})")
lines.append("};")
lines.append("")

open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
print(f"OK: {OUT} ({len(rows)} filas)")
