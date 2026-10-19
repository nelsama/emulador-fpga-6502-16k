#!/usr/bin/env python3
"""emu_agent.py - Lanza y controla el emulador desde un agente o script.

Resuelve los dos problemas que hacen que un agente crea que el emulador
"se cae":

  1. Lanza el emulador DESACOPLADO (Start-Process en Windows), de modo que
     sobreviva al fin del comando del shell.
  2. Sondea hasta que responde, en vez de dormir un tiempo fijo.

Uso:
    python tools/emu_agent.py start [--uart 4000] [--debug 8080] [--sd tarjeta.img]
    python tools/emu_agent.py status
    python tools/emu_agent.py screenshot salida.png
    python tools/emu_agent.py load PACMAN.BIN 0800
    python tools/emu_agent.py run 0800
    python tools/emu_agent.py state
    python tools/emu_agent.py stop

Requisitos: Python 3 (solo librería estándar).
"""

import argparse
import os
import socket
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "emulador.exe" if os.name == "nt" else "emulador")
ROM = os.path.join(ROOT, "rom", "monitor.bin")


# --------------------------------------------------------------------------
# Proceso
# --------------------------------------------------------------------------
def procesos() -> list:
    """PIDs del emulador en ejecución."""
    if os.name == "nt":
        r = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq emulador.exe", "/FO", "CSV", "/NH"],
            capture_output=True, text=True)
        pids = []
        for linea in r.stdout.splitlines():
            partes = linea.split('","')
            if len(partes) > 1 and "emulador.exe" in partes[0]:
                pids.append(partes[1].strip('"'))
        return pids
    r = subprocess.run(["pgrep", "-f", "emulador"], capture_output=True, text=True)
    return r.stdout.split()


def start(uart: int, debug: int, sd: str) -> int:
    if procesos():
        print("Ya hay un emulador en ejecución. Usa 'status' o 'stop'.")
        return 1

    args = [ROM, str(uart)]
    if sd:
        args.append(sd)
    if debug:
        args.append(f"--debug={debug}")

    log = os.path.join(ROOT, ".tmp")
    os.makedirs(log, exist_ok=True)

    if os.name == "nt":
        # Emulador desacoplado: sobrevive al fin de este comando.
        ps = (
            "Start-Process -FilePath '%s' -ArgumentList %s "
            "-RedirectStandardOutput '%s\\emu.log' "
            "-RedirectStandardError '%s\\emu.err' -WindowStyle Normal"
            % (EXE, ",".join("'%s'" % a for a in args), log, log)
        )
        subprocess.run(["powershell", "-Command", ps], check=False)
    else:
        subprocess.Popen([EXE] + args,
                         stdout=open(os.path.join(log, "emu.log"), "w"),
                         stderr=open(os.path.join(log, "emu.err"), "w"),
                         start_new_session=True)

    # Sondear en vez de dormir: hasta 15 s.
    for _ in range(75):
        if debug:
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{debug}/", timeout=1).read()
                print(f"Emulador listo. UART {uart}, debug http://localhost:{debug}")
                return 0
            except OSError:
                pass
        else:
            try:
                s = socket.create_connection(("127.0.0.1", uart), timeout=1)
                s.close()
                print(f"Emulador listo. UART {uart}")
                return 0
            except OSError:
                pass
        time.sleep(0.2)

    print("El emulador no respondió en 15 s. Revisa .tmp/emu.err", file=sys.stderr)
    return 1


def stop() -> int:
    if not procesos():
        print("No hay ningún emulador en ejecución.")
        return 0
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/IM", "emulador.exe"],
                       capture_output=True)
    else:
        subprocess.run(["pkill", "-f", "emulador"], capture_output=True)
    print("Emulador detenido.")
    return 0


# --------------------------------------------------------------------------
# UART
# --------------------------------------------------------------------------
def enviar(uart: int, comando: str, espera: float = 2.5) -> str:
    s = socket.create_connection(("127.0.0.1", uart), timeout=5)
    time.sleep(0.6)          # el monitor manda su prompt al conectar
    s.sendall(comando.encode() + b"\r")
    time.sleep(espera)
    s.settimeout(0.4)
    datos = b""
    try:
        while True:
            c = s.recv(65536)
            if not c:
                break
            datos += c
    except OSError:
        pass
    s.close()
    return datos.decode(errors="replace")


# --------------------------------------------------------------------------
# HTTP
# --------------------------------------------------------------------------
def http(debug: int, ruta: str) -> bytes:
    # 127.0.0.1 en lugar de 'localhost': evita la resolución inicial.
    return urllib.request.urlopen(f"http://127.0.0.1:{debug}{ruta}", timeout=15).read()


def main() -> int:
    p = argparse.ArgumentParser(description="Controla el emulador desde un script.")
    p.add_argument("accion", choices=[
        "start", "stop", "status", "screenshot", "load", "run", "state", "vram", "ram"])
    p.add_argument("arg1", nargs="?", help="argumento (fichero, dirección, ...)")
    p.add_argument("arg2", nargs="?", help="segundo argumento (dirección)")
    p.add_argument("--uart", type=int, default=4000)
    p.add_argument("--debug", type=int, default=8080)
    p.add_argument("--sd", default="tarjeta.img")
    a = p.parse_args()

    if a.accion == "start":
        return start(a.uart, a.debug, a.sd)
    if a.accion == "stop":
        return stop()
    if a.accion == "status":
        pids = procesos()
        print("en ejecución (PID %s)" % ", ".join(pids) if pids else "detenido")
        if pids and a.debug:
            try:
                http(a.debug, "/")
                print(f"HTTP responde en el puerto {a.debug}")
            except OSError:
                print(f"HTTP NO responde en el puerto {a.debug}")
        return 0

    # El resto necesita el emulador vivo.
    if not procesos():
        print("El emulador no está en ejecución. Usa 'start'.", file=sys.stderr)
        return 1

    if a.accion == "screenshot":
        destino = a.arg1 or "pantalla.png"
        datos = http(a.debug, "/screen.png")
        open(destino, "wb").write(datos)
        print(f"{destino} ({len(datos)} bytes)")
    elif a.accion == "load":
        if not a.arg1:
            print("Uso: load <fichero> [direccion]", file=sys.stderr)
            return 2
        addr = a.arg2 or "0800"
        print(enviar(a.uart, f"load {a.arg1} {addr}", 3.0).strip())
    elif a.accion == "run":
        addr = a.arg1 or "0800"
        print(enviar(a.uart, f"r {addr}", 1.5).strip())
    elif a.accion == "state":
        print(http(a.debug, "/state.txt").decode(errors="replace"))
    elif a.accion == "vram":
        print(http(a.debug, "/vram.json").decode(errors="replace"))
    elif a.accion == "ram":
        inicio = int(a.arg1 or "0", 0)
        largo = int(a.arg2 or "256", 0)
        print(http(a.debug, f"/ram.bin?start={inicio}&len={largo}").hex(" "))
    return 0


if __name__ == "__main__":
    sys.exit(main())
