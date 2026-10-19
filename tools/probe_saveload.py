#!/usr/bin/env python3
"""Prueba SAVE/LOAD con un archivo multi-sector (1500 B = 3 sectores)."""
import socket
import sys
import time

port = int(sys.argv[1]) if len(sys.argv) > 1 else 4017
s = socket.create_connection(('127.0.0.1', port), timeout=5)


def drain(t=1.2):
    s.settimeout(t)
    d = b''
    try:
        while True:
            c = s.recv(4096)
            if not c:
                break
            d += c
    except socket.timeout:
        pass
    return d.decode(errors='replace')


def send(text):
    for ch in text.encode():
        s.send(bytes([ch]))
        time.sleep(0.02)


def cmd(text, wait=1.2):
    send(text + '\r')
    return drain(wait)


print('--- ARRANQUE ---')
print(cmd('', 2.5))

# 1. Llenar $0800..$0DDB (1500 bytes) con un patrón: byte = (i*7+3) & 0xFF
#    El monitor no tiene un comando de fill por patrón, así que usamos F (fill)
#    para bloques homogéneos y verificamos por CAT.
print('--- FILL 0800 1500 con AA ---')
print(cmd('F 0800 1500 AA', 2.0))

print('--- SAVE BIG.BIN 0800 1500 (1500 B = 3 sectores) ---')
print(cmd('SAVE BIG.BIN 0800 1500', 2.5))

print('--- LS ---')
print(cmd('LS'))

print('--- CAT BIG.BIN (primeras lineas) ---')
cat = cmd('CAT BIG.BIN', 3.0)
print('\n'.join(cat.splitlines()[:10]))
print('...')

# 2. Corromper esos 1500 bytes con otro valor
print('--- FILL 0800 1500 55 (corromper) ---')
print(cmd('F 0800 1500 55', 2.0))
print(cmd('D 0800 10'))

# 3. Recargar desde la SD
print('--- LOAD BIG.BIN 0800 ---')
print(cmd('LOAD BIG.BIN 0800', 2.5))

# 4. Verificar que volvieron los AA
print('--- D 0800 20 (debe ser AA...) ---')
print(cmd('D 0800 20'))
print('--- D 0D C0 20 (cerca del final) ---')
print(cmd('D 0DC0 20'))

s.close()
