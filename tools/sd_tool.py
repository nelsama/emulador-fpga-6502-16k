#!/usr/bin/env python3
"""sd_tool.py - Gestiona imagenes de tarjeta SD MicroFS del emulador.

La tarjeta emulada es un unico archivo binario de 512*N bytes donde el sector 0
contiene la tabla de archivos MicroFS:

    offset 0-1    : magic "FM" (0x46 0x4D)
    offset 6-7    : siguiente sector libre (little-endian)
    offset 16+    : 16 entradas de 32 bytes
                      +0..11  nombre (ASCII, terminado en NUL)
                      +12..13 sector inicial (LE)
                      +14..15 tamano en bytes (LE)
                      +16..17 sectores reservados (LE)

    Sector 1+     : datos de los archivos (contiguos)

Uso:
    python tools/sd_tool.py list    <imagen.img>
    python tools/sd_tool.py extract <imagen.img> <nombre> [salida]
    python tools/sd_tool.py inject  <imagen.img> <archivo> [nombre_en_sd]
    python tools/sd_tool.py delete  <imagen.img> <nombre>
    python tools/sd_tool.py format  <imagen.img> [tamano_mb]
    python tools/sd_tool.py info    <imagen.img>

Ejemplos:
    python tools/sd_tool.py list tarjeta.img
    python tools/sd_tool.py extract tarjeta.img JUEGO.BIN juego.bin
    python tools/sd_tool.py inject tarjeta.img demo.bin DEMO.BIN
"""

import os
import struct
import sys

SECTOR = 512
MAX_FILES = 16
ENTRY_SIZE = 32
ENTRY_BASE = 16
MAGIC = b"FM"
NAME_LEN = 12


class MicroFS:
    def __init__(self, path):
        self.path = path
        self.data = bytearray(open(path, "rb").read())

    def save(self):
        with open(self.path, "wb") as f:
            f.write(self.data)

    # -- cabecera --------------------------------------------------------
    def is_valid(self):
        return len(self.data) >= SECTOR and bytes(self.data[0:2]) == MAGIC

    def next_sector(self):
        return struct.unpack_from("<H", self.data, 6)[0]

    def set_next_sector(self, v):
        struct.pack_into("<H", self.data, 6, v)

    def total_sectors(self):
        return len(self.data) // SECTOR

    # -- entradas --------------------------------------------------------
    def entry_offset(self, idx):
        return ENTRY_BASE + idx * ENTRY_SIZE

    def entries(self):
        """Devuelve [(indice, nombre, sector, tamano, sectores)]."""
        out = []
        for i in range(MAX_FILES):
            off = self.entry_offset(i)
            name_bytes = bytes(self.data[off:off + NAME_LEN])
            if name_bytes[0] == 0:
                continue
            name = name_bytes.split(b"\0")[0].decode("ascii", "replace")
            start, size, sect = struct.unpack_from("<HHH", self.data, off + 12)
            out.append((i, name, start, size, sect))
        return out

    def find(self, name):
        name = name.upper()
        for (i, n, start, size, sect) in self.entries():
            if n.upper() == name:
                return (i, n, start, size, sect)
        return None

    # -- datos -----------------------------------------------------------
    def read_file(self, start, size):
        off = start * SECTOR
        return bytes(self.data[off:off + size])

    def free_sector(self):
        """Primer sector libre: max(siguiente, fin de cualquier archivo)."""
        nxt = self.next_sector()
        end = 1
        for (_, _, start, size, sect) in self.entries():
            needed = max(sect, (size + SECTOR - 1) // SECTOR or 1)
            end = max(end, start + needed)
        return max(nxt, end, 1)


def fmt_name(name):
    base = name.upper()
    if len(base.encode("ascii")) > NAME_LEN - 1:
        base = base[:NAME_LEN - 1]
    return base


def cmd_list(path):
    fs = MicroFS(path)
    if not fs.is_valid():
        print("ERROR: %s no es una imagen MicroFS valida (falta magic 'FM')" % path)
        return 1
    ents = fs.entries()
    print("Imagen: %s" % path)
    print("Tamano: %d MB (%d sectores)" % (len(fs.data) // (1024 * 1024), fs.total_sectors()))
    print("Siguiente sector libre: %d" % fs.next_sector())
    print()
    if not ents:
        print("  (vacio)")
    else:
        print("  %-12s %10s %8s %s" % ("NOMBRE", "TAMANO", "SECTOR", "SECTORES"))
        for (i, name, start, size, sect) in ents:
            print("  %-12s %10d %8d %d" % (name, size, start, sect))
    print()
    print("Total: %d/%d archivos" % (len(ents), MAX_FILES))
    return 0


def cmd_extract(path, name, out):
    fs = MicroFS(path)
    e = fs.find(name)
    if not e:
        print("ERROR: no existe '%s'" % name)
        return 1
    _, n, start, size, _ = e
    data = fs.read_file(start, size)
    out = out or n.lower()
    with open(out, "wb") as f:
        f.write(data)
    print("OK: %s -> %s (%d bytes)" % (n, out, len(data)))
    return 0


def cmd_inject(path, src, name=None):
    fs = MicroFS(path)
    if not fs.is_valid():
        print("ERROR: imagen no valida. Crea una con 'format' primero.")
        return 1

    data = open(src, "rb").read()
    name = fmt_name(name or os.path.basename(src))

    # Si ya existe, se borra antes.
    e = fs.find(name)
    if e:
        fs_delete(fs, e[1])

    if len(fs.entries()) >= MAX_FILES:
        print("ERROR: tabla llena (%d archivos)" % MAX_FILES)
        return 1

    sectors_needed = (len(data) + SECTOR - 1) // SECTOR or 1
    start = fs.free_sector()
    if (start + sectors_needed) > fs.total_sectors():
        print("ERROR: no cabe en la imagen (%d sectores necesarios, quota=%d)"
              % (sectors_needed, fs.total_sectors() - start))
        return 1

    # Primer slot libre
    slot = None
    for i in range(MAX_FILES):
        if fs.data[fs.entry_offset(i)] == 0:
            slot = i
            break
    if slot is None:
        print("ERROR: no hay slot libre")
        return 1

    off = fs.entry_offset(slot)
    fs.data[off:off + ENTRY_SIZE] = b"\0" * ENTRY_SIZE
    nb = name.encode("ascii")
    fs.data[off:off + len(nb)] = nb
    struct.pack_into("<HHH", fs.data, off + 12, start, len(data), sectors_needed)

    doff = start * SECTOR
    fs.data[doff:doff + len(data)] = data
    # Rellenar el resto del ultimo sector con ceros
    end = doff + sectors_needed * SECTOR
    fs.data[doff + len(data):end] = b"\0" * (end - doff - len(data))

    fs.set_next_sector(start + sectors_needed)
    fs.save()
    print("OK: %s en sector %d (%d bytes, %d sectores)"
          % (name, start, len(data), sectors_needed))
    return 0


def fs_delete(fs, name):
    e = fs.find(name)
    if not e:
        return False
    i = e[0]
    off = fs.entry_offset(i)
    fs.data[off:off + ENTRY_SIZE] = b"\0" * ENTRY_SIZE
    return True


def cmd_delete(path, name):
    fs = MicroFS(path)
    if not fs_delete(fs, name):
        print("ERROR: no existe '%s'" % name)
        return 1
    fs.save()
    print("OK: '%s' eliminado" % name.upper())
    return 0


def cmd_format(path, size_mb=16):
    sectors = size_mb * 1024 * 2
    data = bytearray(sectors * SECTOR)
    data[0:2] = MAGIC
    struct.pack_into("<H", data, 6, 1)
    if os.path.exists(path):
        print("AVISO: sobrescribiendo %s" % path)
    with open(path, "wb") as f:
        f.write(data)
    print("OK: imagen MicroFS creada, %d MB (%d sectores)" % (size_mb, sectors))
    return 0


def cmd_info(path):
    fs = MicroFS(path)
    if not fs.is_valid():
        print("ERROR: no es una imagen MicroFS")
        return 1
    ents = fs.entries()
    used = sum(s for (_, _, _, _, s) in ents)
    total = fs.total_sectors()
    print("Archivo : %s" % path)
    print("Tamano  : %d MB" % (len(fs.data) // (1024 * 1024)))
    print("Sectores: %d totales, %d en uso (%d archivos)" % (total, used, len(ents)))
    print("Libres  : %d sectores (%.1f MB)" %
          (total - used, (total - used) * SECTOR / (1024 * 1024)))
    return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, path = argv[1], argv[2]
    rest = argv[3:]

    if not os.path.exists(path) and cmd != "format":
        print("ERROR: no existe %s" % path)
        return 1

    if cmd == "list":
        return cmd_list(path)
    if cmd == "extract":
        if not rest:
            print("Falta el nombre del archivo a extraer")
            return 1
        return cmd_extract(path, rest[0], rest[1] if len(rest) > 1 else None)
    if cmd == "inject":
        if not rest:
            print("Falta el archivo a inyectar")
            return 1
        return cmd_inject(path, rest[0], rest[1] if len(rest) > 1 else None)
    if cmd == "delete":
        if not rest:
            print("Falta el nombre del archivo")
            return 1
        return cmd_delete(path, rest[0])
    if cmd == "format":
        mb = int(rest[0]) if rest else 16
        return cmd_format(path, mb)
    if cmd == "info":
        return cmd_info(path)

    print("Comando desconocido: %s" % cmd)
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
