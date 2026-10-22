"""Genera src/terminal/font_unicode.h: fuente 8x16 con los glifos Unicode
que el panel del emulador necesita para dibujar lo que manda un programa.

Por qué existe: el panel dibuja la consola como un terminal. Los programas
mandan texto por el UART, y algunos (no todos) usan UTF-8. La tabla CP437
(font_cp437.h) cubre el sistema clasico, pero no fichas de ajedrez ni otros
simbolos Unicode. Esta fuente añade esos rangos SIN que el emulador sepa nada
de ninguna aplicacion: solo cubre intervalos de Unicode.

Rangos incluidos (ver RANGOS). Si un caracter no esta, el terminal pinta '?'
en una sola celda; eso lo decide terminal_state.cpp, no este archivo.

Formato de salida: un array de bytes, 16 por glifo (una fila por byte, bit 7 =
pixel mas a la izquierda), igual que kFontC64 y kFontCp437. Se acompaña de una
tabla de indices (punto de codigo -> offset en el array) para no reservar
espacio para los huecos.

Fuente: DejaVu Sans Mono (licencia Bitstream Vera, permite redistribuir).
Los glifos se rasterizan a 1 bit y se centran en la celda 8x16.

Uso:
    python tools/gen_font_unicode.py [ruta_ttf]

Requiere Pillow. La fuente NO se versiona en el repo (es grande): se descarga
una vez para generar el .h, y el .h si se versiona.
"""

import sys

from PIL import Image, ImageDraw, ImageFont

# Rangos Unicode a incluir. Añadir aqui lo que el panel tenga que mostrar.
RANGOS = [
    (0x00A0, 0x00FF),  # Latin-1 suplementario: acentos, simbolos
    (0x2190, 0x2195),  # flechas: arriba, abajo, izquierda, derecha
    (0x2500, 0x257F),  # Box Drawing: marcos de las interfaces de texto
    (0x2580, 0x259F),  # Block Elements: barras y tramados
    (0x25A0, 0x25CF),  # formas geometricas (incluye cuadrados y circulos)
    (0x2654, 0x265F),  # fichas de ajedrez (blancas y negras)
]

ANCHO, ALTO = 8, 16


def glifos_objetivo():
    """Lista ordenada de puntos de codigo a rasterizar, sin duplicados."""
    vistos = []
    for ini, fin in RANGOS:
        for cp in range(ini, fin + 1):
            if cp not in vistos:
                vistos.append(cp)
    return vistos


def rasterizar(fuente, cp):
    """Devuelve 16 bytes (una fila por byte) con el glifo centrado en 8x16."""
    img = Image.new("L", (ANCHO, ALTO), 0)
    d = ImageDraw.Draw(img)
    try:
        d.text((0, 0), chr(cp), fill=255, font=fuente)
    except Exception:
        return None
    # Centrado vertical: se recorta el glifo y se coloca a media celda.
    bbox = img.getbbox()
    if bbox:
        x0, y0, x1, y1 = bbox
        alto_glifo = y1 - y0
        dy = (ALTO - alto_glifo) // 2 - y0
        if dy:
            img = img.crop((0, -dy, ANCHO, ALTO - dy))
    filas = []
    for y in range(ALTO):
        b = 0
        for x in range(ANCHO):
            if img.getpixel((x, y)) > 96:
                b |= 0x80 >> x
        filas.append(b)
    return filas


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else "DejaVuSansMono.ttf"
    # Se rasteriza a 13 px: en una celda de 16 sobra un pixel arriba y otro
    # abajo, que es lo que hace legible el texto a 8 de ancho.
    fuente = ImageFont.truetype(ruta, 13)

    cps = glifos_objetivo()
    datos = []
    indices = {}
    for cp in cps:
        filas = rasterizar(fuente, cp)
        if filas is None:
            continue
        indices[cp] = len(datos)
        datos.extend(filas)

    lineas = []
    lineas.append("// Generado por tools/gen_font_unicode.py. NO editar a mano.")
    lineas.append("//")
    lineas.append("// Fuente: DejaVu Sans Mono (licencia Bitstream Vera).")
    lineas.append("// %d glifos, %d bytes." % (len(indices), len(datos)))
    lineas.append("//")
    lineas.append("// Cada glifo ocupa 16 bytes (una fila por byte, bit 7 = pixel")
    lineas.append("// izquierdo), igual que kFontCp437. La tabla de indices traduce")
    lineas.append("// punto de codigo Unicode -> offset dentro de kFontUnicode.")
    lineas.append("#pragma once")
    lineas.append("")
    lineas.append("#include <cstdint>")
    lineas.append("")
    lineas.append("namespace font_unicode {")
    lineas.append("")
    lineas.append("struct Indice { uint32_t cp; uint16_t offset; };")
    lineas.append("")
    lineas.append("static const Indice kIndices[] = {")
    for cp in sorted(indices):
        lineas.append("    {0x%04X, %d}," % (cp, indices[cp]))
    lineas.append("};")
    lineas.append("static const int kNumIndices = %d;" % len(indices))
    lineas.append("")
    lineas.append("static const uint8_t kGlifos[] = {")
    for i in range(0, len(datos), 16):
        trozo = datos[i:i + 16]
        lineas.append("    " + ", ".join("0x%02X" % b for b in trozo) + ",")
    lineas.append("};")
    lineas.append("")
    lineas.append("} // namespace font_unicode")
    lineas.append("")

    salida = "src/terminal/font_unicode.h"
    with open(salida, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lineas))
    print("%s: %d glifos, %d bytes" % (salida, len(indices), len(datos)))


if __name__ == "__main__":
    main()
