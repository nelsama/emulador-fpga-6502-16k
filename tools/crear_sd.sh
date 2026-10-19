#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Regenera tarjeta.img a partir de sdcard/
#
#  La imagen de la SD no se versiona (16 MB, binaria). Su contenido legible está
#  en sdcard/ y se reconstruye con este script.
#
#  Uso:
#    ./tools/crear_sd.sh            -> crea tarjeta.img (16 MB)
#    ./tools/crear_sd.sh otra.img   -> otro nombre
#
#  Requiere Python 3.
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")/.."

IMG="${1:-tarjeta.img}"
DIR="sdcard"

if [ ! -d "$DIR" ]; then
  echo "[!] No existe el directorio $DIR"
  exit 1
fi

echo "Creando $IMG (16 MB)..."
python tools/sd_tool.py format "$IMG" 16

for f in "$DIR"/*; do
  [ -f "$f" ] || continue
  name=$(basename "$f")
  echo "  + $name"
  python tools/sd_tool.py inject "$IMG" "$f" "$name" >/dev/null
done

echo
python tools/sd_tool.py list "$IMG"
echo
echo "Listo: $IMG"
