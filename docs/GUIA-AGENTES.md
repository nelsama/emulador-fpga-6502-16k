# Guía para agentes de IA

Este documento es para **agentes automáticos** (o cualquier herramienta) que
usen el emulador para desarrollar y depurar software del `fpga-6502-16k`.

Responde a una pregunta: *¿qué puede ver un agente del estado de la máquina y
cómo, sin abrir la ventana ni leer una captura a ojo?*

La guía de uso completa está en [`GUIA-USO.md`](GUIA-USO.md).

---

## 1. Hay dos pantallas distintas

Es lo primero que hay que entender, y el error más común:

| Superficie | La usa | Cómo verla | Contenido |
|---|---|---|---|
| **Core de Vídeo** (VRAM) | juegos gráficos (Pac-Man, Tetris gráfico, River Raid) | `/screen.png`, `/vram.txt` | tiles, sprites, scroll, paletas |
| **Consola** (UART) | programas de terminal (Tetris de consola, editor, monitor) | `/console.txt`, `/console.png` | texto 80×24, colores ANSI, CP437 |

**Son independientes.** Un juego gráfico no escribe en la consola (salvo mensajes
de depuración), y un programa de terminal no dibuja en la VRAM. Si tu programa
"no se ve", comprueba primero **en cuál de las dos debería aparecer**.

---

## 2. Arranque y control

```bash
python tools/emu_agent.py start          # lanza desacoplado y espera a que responda
python tools/emu_agent.py load PACMAN.BIN 0800
python tools/emu_agent.py run 0800
python tools/emu_agent.py screenshot pantalla.png
python tools/emu_agent.py state          # /state.txt
python tools/emu_agent.py stop
```

El servidor de depuración solo se activa con `run_debug.*` o `--debug`
(`--debug=PUERTO` para otro puerto). Escucha en **`http://localhost:8080`**.

Para lanzarlo a mano (Windows), **desacoplado**:

```powershell
Start-Process -FilePath '.\emulador.exe' `
  -ArgumentList 'rom/monitor.bin','4000','tarjeta.img','--debug' `
  -RedirectStandardOutput '.tmp\out.log' -RedirectStandardError '.tmp\err.log'
```

### Cargar un programa por el UART

```python
import socket, time
s = socket.create_connection(('127.0.0.1', 4000), 3)
s.sendall(b'load PACMAN.BIN 0800\r'); time.sleep(2.0)
s.sendall(b'r 0800\r'); time.sleep(3.0)
```

La **dirección de carga debe coincidir con la de enlace del binario**. Si no,
el programa ejecuta basura y no avisa.

---

## 3. Qué puede leer un agente

| Endpoint | Para qué |
|---|---|
| `/console.txt` | **Lo más útil** para programas de texto: la pantalla de 80×24 en texto plano |
| `/console.png` | Captura de la consola con colores (640×384), `?scale=2..4` |
| `/screen.png` | Captura del vídeo del FPGA (320×240), `?scale=2..4` |
| `/vram.txt` | Tilemap, atributos y OAM en hexadecimal + vista ASCII 40×30 |
| `/vram.json` | Lo mismo, estructurado |
| `/state.txt` | CPU/PC, GPIO (joystick + LEDs), Core de Vídeo, SID, UART |
| `/ram.bin` | RAM cruda (`?start=&len=`) |

### Ejemplos

```bash
curl http://localhost:8080/console.txt
curl -o ojo.png "http://localhost:8080/screen.png?scale=2"
curl http://localhost:8080/state.txt | head -40
```

`/state.txt` incluye la tabla de pines del puerto 1 (bit, pin del FPGA, señal,
modo entrada/salida y nivel), que es lo que hace falta para depurar el joystick:
si los bits 3-7 no están en `entrada`, el programa no llamó a `joy_init()`.

---

## 4. Comprobar que el programa corre

1. `/state.txt` → mira `PC`. Si no se mueve entre dos lecturas, el programa está
   colgado (bucle sin retorno, esperando una IRQ que no llega, o `JMP` a un
   vector que no existe).
2. Si `PC` avanza pero no se ve nada: mira la **superficie correcta** (tabla del
   punto 1) y luego:
   - vídeo: `/vram.txt`, campo `tiles escritos`;
   - consola: `/console.txt`, puede estar limpia porque el programa no ha
     escrito nada o ha hecho `ESC[2J`.
3. Si hay basura en la consola: mira si el programa emite secuencias ANSI no
   soportadas o bytes en otra codificación (este sistema usa **CP437**, no
   UTF-8).

---

## 5. Trampas conocidas

| Trampa | Qué pasa | Qué hacer |
|---|---|---|
| Lanzar con `(... &)` desde un shell | el proceso muere al cerrar el shell; **no es un fallo del emulador** | lanzar desacoplado (`Start-Process`, `setsid`, `emu_agent.py start`) |
| `emulador.exe` en ejecución | `make` falla con *Permission denied* al enlazar | cerrar el emulador antes de compilar |
| Dos instancias abiertas | la segunda no abre el puerto 4000 y `connection refused` | comprobar con `tasklist` y cerrar las sobrantes |
| Esperar salida antes de cargar | el monitor ya escribió su banner al arrancar | el banner se pierde si conectas tarde; usa `/console.txt` para el estado actual |
| Capturar demasiado pronto | el programa aún está cargando | esperar ~2 s tras `load` y ~3 s tras `r` |
| Creer que el ANSI no llega | el texto sí llega; el que interpreta es el host | este emulador **sí** interpreta ANSI (CUP, ED, EL, SGR) y CP437 |
| Buscar Unicode | no existe en esta plataforma | el cable mueve bytes sueltos; usa CP437 |

---

## 6. Qué NO hace el emulador

- No interpreta UTF-8: **CP437** para bytes ≥ 0x80.
- La consola es de **solo lectura**: no manda nada al programa ni acepta teclado.
  Para escribir en el programa usa el UART (socket 4000) o el joystick.
- El ANSI cubierto es el habitual de consola: no hay regiones de scroll,
  ratón ni consultas de estado del cursor.
- La consola es **80×24 fija** (con scrollback interno), no redimensionable por
  el programa.

---

## 7. Formatos de la consola

- `/console.txt`: 24 líneas de 80 caracteres + `\n` cada una. Los bytes ≥ 0x80
  salen **tal cual** (son CP437), no como UTF-8.
- `/console.png`: 640×384 píxeles (celda 8×16), colores ANSI de 16 colores,
  fondo negro. Mismo resultado que ver la ventana.

Para comparar con lo que ve un humano por PuTTY: PuTTY debe estar en modo DOS
(CP437). Ver [`PUTTY-SETUP.md`](PUTTY-SETUP.md).

---

## 8. Pruebas sin abrir la ventana

```bash
make test                       # intérprete de terminal (17 casos)
./test_terminal.exe ROM.bin     # además interpreta un binario
```

Útil para comprobar cambios en el emulador sin lanzar la ventana ni el
emulador completo.
