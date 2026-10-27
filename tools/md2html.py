#!/usr/bin/env python3
"""md2html.py - Convierte el manual de Markdown a un HTML autocontenido.

Uso:
    python tools/md2html.py docs/GUIA-USO.md dist/MANUAL.html "Emulador FPGA 6502"

Lo usa `make dist` para dejar el manual junto al ejecutable. El HTML lleva el
CSS embebido y no carga nada externo, así que se abre con doble clic sin
conexión. Si hay imágenes en `docs/img/`, se incrustan como base64 para que el
fichero siga siendo único.

Requiere el paquete `markdown` (pip install markdown).
"""

import base64
import os
import re
import sys

try:
    import markdown
except ImportError:
    sys.exit("md2html.py: falta el paquete 'markdown' (pip install markdown)")

CSS = """
:root { color-scheme: light dark; }
body {
  font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  line-height: 1.6; max-width: 60rem; margin: 0 auto; padding: 2rem 1.5rem 5rem;
  color: #1f2328; background: #fff;
}
@media (prefers-color-scheme: dark) {
  body { color: #d6d9dd; background: #17191c; }
  a { color: #6cb6ff; }
  th { background: #22252a; }
  tr:nth-child(even) td { background: #1d2024; }
  code { background: #22252a; }
  pre { background: #101215; border-color: #2a2e34; }
  blockquote { border-color: #3a3f46; color: #a9b0b8; }
  hr { border-color: #2a2e34; }
}
h1, h2, h3, h4 { line-height: 1.25; margin-top: 2.2rem; }
h1 { font-size: 1.9rem; border-bottom: 2px solid currentColor; padding-bottom: .4rem; }
h2 { font-size: 1.45rem; border-bottom: 1px solid #8884; padding-bottom: .3rem; }
h3 { font-size: 1.15rem; }
a { color: #0969da; text-decoration: none; }
a:hover { text-decoration: underline; }
code {
  font-family: "Cascadia Mono", Consolas, "Courier New", monospace;
  font-size: .88em; background: #eff1f3; padding: .15em .35em; border-radius: 4px;
}
pre {
  background: #f6f8fa; border: 1px solid #d8dee4; border-radius: 6px;
  padding: .9rem 1rem; overflow-x: auto;
}
pre code { background: none; padding: 0; font-size: .85em; }
table { border-collapse: collapse; width: 100%; margin: 1.1rem 0; font-size: .94em; }
th, td { border: 1px solid #8884; padding: .45rem .7rem; text-align: left; vertical-align: top; }
th { background: #f0f2f4; font-weight: 600; }
tr:nth-child(even) td { background: #fafbfc; }
blockquote {
  margin: 1.1rem 0; padding: .3rem 1rem; border-left: 4px solid #d0d7de; color: #57606a;
}
blockquote > :first-child { margin-top: 0; }
blockquote > :last-child { margin-bottom: 0; }
hr { border: 0; border-top: 1px solid #d0d7de; margin: 2.5rem 0; }
img { max-width: 100%; height: auto; }
"""


def incrustar_imagenes(html: str, base_dir: str) -> str:
    """Sustituye <img src="img/x.png"> por un data URI base64."""
    def reemplazo(m):
        ruta = m.group(1)
        completa = os.path.join(base_dir, ruta)
        if not os.path.isfile(completa):
            return m.group(0)  # se deja igual; el manual no depende de ella
        ext = os.path.splitext(completa)[1].lower().lstrip(".")
        if ext == "jpg":
            ext = "jpeg"
        with open(completa, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("ascii")
        return m.group(0).replace(ruta, f"data:image/{ext};base64,{b64}")
    return re.sub(r'<img[^>]+src="([^"]+)"', reemplazo, html)


def convertir(entrada: str, salida: str, titulo: str) -> None:
    with open(entrada, encoding="utf-8") as f:
        texto = f.read()

    # `markdown` hace Tablas, bloques de código con cercas y listas; no hace
    # anclas de encabezado ni generar el <html>, y eso se añade aquí.
    html = markdown.markdown(
        texto,
        extensions=["tables", "fenced_code", "toc", "sane_lists"],
        extension_configs={"toc": {"permalink": False}},
    )
    html = incrustar_imagenes(html, os.path.dirname(os.path.abspath(entrada)))

    documento = f"""<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{titulo}</title>
<style>{CSS}</style>
</head>
<body>
{html}
</body>
</html>
"""
    os.makedirs(os.path.dirname(os.path.abspath(salida)), exist_ok=True)
    with open(salida, "w", encoding="utf-8", newline="\n") as f:
        f.write(documento)
    print(f"Manual: {salida} ({len(documento) // 1024} KB)")


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 1
    entrada, salida = sys.argv[1], sys.argv[2]
    titulo = sys.argv[3] if len(sys.argv) > 3 else "Manual"
    convertir(entrada, salida, titulo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
