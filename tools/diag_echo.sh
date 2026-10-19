#!/bin/bash
# Diagnóstico del eco de caracteres: envía "ls\r" y muestra todo.
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-4092}
rm -f .tmp/t5.img
./emulador.exe rom/monitor.bin "$PORT" .tmp/t5.img > .tmp/e6.log 2>&1 &
PID=$!
sleep 3

python3 - <<EOF
import socket, time
s = socket.create_connection(('127.0.0.1', $PORT), timeout=5)
s.settimeout(0.8)
# drenar banner
d = b''
try:
    while True:
        c = s.recv(4096)
        if not c: break
        d += c
except Exception:
    pass
print('banner: %d bytes, termina en %r' % (len(d), d[-10:]))

for ch in b'l':
    s.send(bytes([ch])); time.sleep(0.5)
    o = b''
    try:
        while True:
            c = s.recv(4096)
            if not c: break
            o += c
    except Exception:
        pass
    print('tras %r -> %r' % (chr(ch), o))

for ch in b's\r':
    s.send(bytes([ch])); time.sleep(0.5)
    o = b''
    try:
        while True:
            c = s.recv(4096)
            if not c: break
            o += c
    except Exception:
        pass
    print('tras %r -> %r' % (chr(ch), o))

s.close()
EOF

kill $PID 2>/dev/null
