#!/bin/bash
# Prueba "h", "ls", "h" con distintos ritmos para reproducir el problema.
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-4094}
rm -f .tmp/t6.img
./emulador.exe rom/monitor.bin "$PORT" .tmp/t6.img > .tmp/e7.log 2>&1 &
PID=$!
sleep 3

python3 - <<EOF
import socket, time
s = socket.create_connection(('127.0.0.1', $PORT), timeout=5)
s.settimeout(0.6)

def drain():
    d = b''
    try:
        while True:
            c = s.recv(4096)
            if not c: break
            d += c
    except Exception:
        pass
    return d

drain()  # banner

def send(text, delay):
    print('>>> enviando %r (delay %.2fs)' % (text, delay))
    for ch in text.encode():
        s.send(bytes([ch]))
        time.sleep(delay)
    time.sleep(0.8)
    out = drain()
    print(repr(out[:300]))
    print()

send('h\r', 0.02)
send('ls\r', 0.02)
send('h\r', 0.02)
s.close()
EOF

kill $PID 2>/dev/null
