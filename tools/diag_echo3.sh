#!/bin/bash
# Envía "ls\r" en un solo paquete TCP (como hace PuTTY).
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-4100}
rm -f .tmp/t7.img
./emulador.exe rom/monitor.bin "$PORT" .tmp/t7.img > .tmp/e8.log 2>&1 &
PID=$!
sleep 3

python3 - <<EOF
import socket, time
s = socket.create_connection(('127.0.0.1', $PORT), timeout=5)
s.settimeout(0.8)
try:
    while True:
        if not s.recv(4096): break
except Exception: pass

# Envío en UN solo send (como PuTTY con tecleo rápido)
s.send(b'ls\r')
time.sleep(1.5)
out = b''
try:
    while True:
        c = s.recv(4096)
        if not c: break
        out += c
except Exception: pass
print('UN PAQUETE "ls\\\\r":')
print(repr(out))
print(out.decode(errors='replace'))
s.close()
EOF

kill $PID 2>/dev/null
