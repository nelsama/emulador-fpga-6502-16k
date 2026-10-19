#!/bin/bash
# Prueba cómo se comporta el monitor al recibir "ls" con distintos timings.
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-4090}
rm -f .tmp/t4.img
./emulador.exe rom/monitor.bin "$PORT" .tmp/t4.img > .tmp/e5.log 2>&1 &
PID=$!
sleep 3

python3 - <<EOF
import socket, time

def run(delay, label):
    s = socket.create_connection(('127.0.0.1', $PORT), timeout=5)
    s.settimeout(1.5)
    # drenar banner
    try:
        while True:
            if not s.recv(4096):
                break
    except Exception:
        pass

    # enviar "ls" caracter a caracter
    for ch in b'ls':
        s.send(bytes([ch]))
        time.sleep(delay)
    s.send(b'\r')
    time.sleep(1.5)
    out = b''
    try:
        while True:
            c = s.recv(4096)
            if not c:
                break
            out += c
    except Exception:
        pass
    print('=== %s (delay %.3fs) ===' % (label, delay))
    print(out.decode(errors='replace').strip()[:400])
    print()
    s.close()

run(0.0,    'sin espera')
run(0.05,   'delay 50 ms')
run(0.2,    'delay 200 ms')
run(0.5,    'delay 500 ms')
EOF

kill $PID 2>/dev/null
