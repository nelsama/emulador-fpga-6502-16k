"""Genera src/terminal/font_cp437.h: fuente 8x16 con los 256 glifos de CP437.

Por qué CP437: es la tabla de códigos que usa la consola de DOS/IBM y la que
asumen los programas de este sistema (el Tetris de consola, por ejemplo).
PuTTY también la usa por defecto en modo DOS, así que con esta tabla el
terminal del emulador dibuja los mismos caracteres que vería el usuario
conectado por el puerto serie.

Formato de salida: un array de 256*16 bytes. Cada glifo ocupa 16 bytes
consecutivos (una fila por byte); el bit 7 es el píxel más a la izquierda, igual
que kFontC64 en src/video/font_c64.h.

Los glifos se rasterizan con una fuente monoespaciada del sistema (Consolas por
defecto), centrados en la celda de 8x16 y umbralizados a 1 bit. Las excepciones
son los bloques sólidos y los tramados (0xB0-0xB2, 0xDB, 0xDC, 0xDE, 0xDD,
0xDF): se dibujan a mano porque son patrones geométricos exactos y una fuente
de texto los deja con márgenes o bordes sucios.

Uso:
    python tools/gen_font_cp437.py [ruta_ttf]
"""

import sys

from PIL import Image, ImageDraw, ImageFont

ANCHO, ALTO = 8, 16
TTF = sys.argv[1] if len(sys.argv) > 1 else "C:/Windows/Fonts/consola.ttf"

# Tabla CP437: byte -> carácter Unicode.
CP437 = (
    "\u0000\u263a\u263b\u2665\u2666\u2663\u2660\u2022\u25d8\u25cb\u25d9\u2642\u2640\u266a\u266b\u263c"
    "\u25ba\u25c4\u2195\u203c\u00b6\u00a7\u25ac\u21b5\u2191\u2193\u2192\u2190\u221f\u2194\u25b2\u25bc"
    " !\"#$%&'()*+,-./0123456789:;<=>?"
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    "`abcdefghijklmnopqrstuvwxyz{|}~\u2302"
    "\u00c7\u00fc\u00e9\u00e2\u00e4\u00e0\u00e5\u00e7\u00ea\u00eb\u00e8\u00ef\u00ee\u00ec\u00c4\u00c5"
    "\u00c9\u00e6\u00c6\u00f4\u00f6\u00f2\u00fb\u00f9\u00ff\u00d6\u00dc\u00a2\u00a3\u00a5\u20a7\u0192"
    "\u00e1\u00ed\u00f3\u00fa\u00f1\u00d1\u00aa\u00ba\u00bf\u2310\u00ac\u00bd\u00bc\u00a1\u00ab\u00bb"
    "\u2591\u2592\u2593\u2502\u2524\u2561\u2562\u2556\u2555\u2563\u2551\u2557\u255d\u255c\u255b\u2510"
    "\u2514\u2534\u252c\u251c\u2500\u253c\u255e\u255f\u255a\u2554\u2569\u2566\u2560\u2550\u256c\u2567"
    "\u2568\u2564\u2565\u2559\u2558\u2552\u2553\u256b\u256a\u2518\u250c\u2588\u2584\u258c\u2590\u2580"
    "\u03b1\u00df\u0393\u03c0\u03a3\u03c3\u00b5\u03c4\u03a6\u0398\u03a9\u03b4\u221e\u03c6\u03b5\u2229"
    "\u2261\u00b1\u2265\u2264\u2320\u2321\u00f7\u2248\u00b0\u2219\u00b7\u221a\u207f\u00b2\u25a0\u00a0"
)

assert len(CP437) == 256, len(CP437)


def bloque(nombre):
    """Bloques y tramados, dibujados a mano (16 filas de 8 bits)."""
    filas = [0] * ALTO
    if nombre == "full":
        return [0xFF] * ALTO
    if nombre == "upper":
        return [0xFF] * 8 + [0x00] * 8
    if nombre == "lower":
        return [0x00] * 8 + [0xFF] * 8
    if nombre == "left":
        return [0xF0] * ALTO
    if nombre == "right":
        return [0x0F] * ALTO
    for y in range(ALTO):
        for x in range(ANCHO):
            if nombre == "shade1":       # ░ 1 de cada 4
                on = (x % 4 == 0 and y % 4 == 0)
            elif nombre == "shade2":     # ▒ 1 de cada 2 (damero)
                on = (x % 2 == 0 and y % 2 == 0)
            else:                        # ▓ 3 de cada 4
                on = not (x % 2 == 1 and y % 2 == 1)
            if on:
                filas[y] |= 0x80 >> x
    return filas


# Bytes CP437 que no salen de la fuente.
A_MANO = {
    0xB0: "shade1",
    0xB1: "shade2",
    0xB2: "shade3",
    0xDB: "full",
    0xDC: "lower",
    0xDD: "left",
    0xDE: "left",   # no se usa en este sistema; aproximación válida
    0xDF: "upper",
}

# Marcos y cajas.
#
# Se dibujan a mano, con trazo de 1 px, para que al encadenar celdas queden
# continuos. El trazo va centrado en la celda: columna 3 (borde derecho de la
# mitad izquierda) y fila 7.
#
# Cada glifo se describe por los lados que tiene: lista de 4 booleanos
# (izquierda, derecha, arriba, abajo). Las esquinas llevan dos lados.
CX, CY = 3, 7   # columna y fila del trazo


def marco(lados):
    """lados = (izq, der, arriba, abajo). Devuelve 16 filas de 8 bits."""
    izq, der, arr, aba = lados
    filas = [0] * ALTO
    # Trazo horizontal: del borde izquierdo al derecho si tiene ambos lados,
    # o solo la mitad correspondiente.
    if izq or der:
        x0 = 0 if izq else CX
        x1 = 7 if der else CX
        for x in range(x0, x1 + 1):
            filas[CY] |= 0x80 >> x
    # Trazo vertical: de arriba a abajo, o solo la mitad correspondiente.
    if arr or aba:
        y0 = 0 if arr else CY
        y1 = 15 if aba else CY
        for y in range(y0, y1 + 1):
            filas[y] |= 0x80 >> CX
    return filas


# byte -> (izq, der, arriba, abajo)
MARCOS = {
    0xB3: (False, False, True, True),    # │
    0xC4: (True, True, False, False),    # ─
    0xDA: (False, True, False, True),    # ┌
    0xBF: (True, False, False, True),    # ┐
    0xC0: (False, True, True, False),    # └
    0xD9: (True, False, True, False),    # ┘
    0xC3: (False, True, True, True),     # ├
    0xB4: (True, False, True, True),     # ┤
    0xC2: (True, True, False, True),     # ┬
    0xC1: (True, True, True, False),     # ┴
    0xC5: (True, True, True, True),      # ┼
    0xBA: (False, False, True, True),    # ║ (se aproxima a │)
    0xCD: (True, True, False, False),    # ═ (se aproxima a ─)
}


def bloque(nombre):
    """Bloques y tramados, dibujados a mano (16 filas de 8 bits)."""
    filas = [0] * ALTO
    if nombre == "full":
        return [0xFF] * ALTO
    if nombre == "upper":
        return [0xFF] * 8 + [0x00] * 8
    if nombre == "lower":
        return [0x00] * 8 + [0xFF] * 8
    if nombre == "left":
        return [0xF0] * ALTO
    if nombre == "right":
        return [0x0F] * ALTO
    for y in range(ALTO):
        for x in range(ANCHO):
            if nombre == "shade1":       # ░ 1 de cada 4
                on = (x % 4 == 0 and y % 4 == 0)
            elif nombre == "shade2":     # ▒ 1 de cada 2 (damero)
                on = (x % 2 == 0 and y % 2 == 0)
            else:                        # ▓ 3 de cada 4
                on = not (x % 2 == 1 and y % 2 == 1)
            if on:
                filas[y] |= 0x80 >> x
    return filas


# Bytes CP437 que no salen de la fuente.
A_MANO = {
    0xB0: "shade1",
    0xB1: "shade2",
    0xB2: "shade3",
    0xDB: "full",
    0xDC: "lower",
    0xDD: "left",
    0xDE: "left",   # no se usa en este sistema; aproximación válida
    0xDF: "upper",
}

def rasterizar(fuente, ch):
    """Pinta un carácter centrado en una celda 8x16 y devuelve 16 bytes."""
    img = Image.new("L", (ANCHO, ALTO), 0)
    ImageDraw.Draw(img).text((ANCHO // 2, ALTO // 2), ch, fill=255,
                             font=fuente, anchor="mm")
    filas = []
    for y in range(ALTO):
        byte = 0
        for x in range(ANCHO):
            if img.getpixel((x, y)) >= 110:
                byte |= 0x80 >> x
        filas.append(byte)
    return filas


def main():
    fuente = ImageFont.truetype(TTF, 13)
    datos = []
    for c in range(256):
        if c == 0:
            datos.extend([0x00] * ALTO)          # NUL: invisible
        elif c in A_MANO:
            datos.extend(bloque(A_MANO[c]))
        elif c in MARCOS:
            datos.extend(marco(MARCOS[c]))
        else:
            datos.extend(rasterizar(fuente, CP437[c]))

    lineas = [
        "#pragma once",
        "",
        "// Fuente 8x16 con los 256 glifos de CP437 (tabla de codigos de la consola",
        "// IBM/DOS). La usa el terminal del panel de depuracion, que dibuja el flujo",
        "// del UART tal como lo veria PuTTY configurado en CP437.",
        "//",
        "// Generada por tools/gen_font_cp437.py. No editar a mano.",
        "// Cada glifo ocupa 16 bytes (una fila por byte, bit 7 = pixel izquierdo).",
        "",
        "static const unsigned char kFontCp437[256 * 16] = {",
    ]
    for c in range(256):
        fila = datos[c * 16:(c + 1) * 16]
        hexs = ", ".join(f"0x{b:02X}" for b in fila)
        lineas.append(f"  {hexs},  // 0x{c:02X}")
    lineas += ["};", ""]

    with open("src/terminal/font_cp437.h", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lineas))
    print(f"OK: src/terminal/font_cp437.h ({256 * 16} bytes de glifos)")


if __name__ == "__main__":
    main()
