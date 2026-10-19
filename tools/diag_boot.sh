#!/bin/bash
# Reproduce el arranque y muestra todo lo que envía el monitor por UART.
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-4080}
rm -f .tmp/t3.img
./emulador.exe rom/monitor.bin "$PORT" .tmp/t3.img > .tmp/e4.log 2>&1 &
PID=$!
sleep 3
python3 - <<EOF
import socket, time
s = socket.create_connection(('127.0.0.1', $PORT), timeout=5)
s.settimeout(3)
data = b''
end = time.time() + 4
while time.time() < end:
    try:
        c = s.recv(4096)
        if not c:
            break
        data += c
    except socket.timeout:
        continue
print('BYTES RECIBIDOS:', len(data))
print(repr(data[:600]))
print('--- texto ---')
print(data.decode(errors='replace')[:800])
s.close()
EOF
kill $PID 2>/dev/null
echo '=== log emulador ==='
cat .tmp/e4.log
