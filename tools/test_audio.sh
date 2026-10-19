#!/bin/bash
# Arranca el emulador, comprueba el banner del monitor y el audio.
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-4071}
rm -f .tmp/t2.img
./emulador.exe rom/monitor.bin "$PORT" .tmp/t2.img > .tmp/e3.log 2>&1 &
PID=$!
sleep 4
python3 - <<EOF
import socket
s = socket.create_connection(('127.0.0.1', $PORT), timeout=5)
s.settimeout(2)
d = b''
try:
    while True:
        c = s.recv(4096)
        if not c:
            break
        d += c
except Exception:
    pass
print(d.decode(errors='replace'))
s.close()
EOF
kill $PID 2>/dev/null
sleep 1
echo '=== log del emulador ==='
head -12 .tmp/e3.log
