# Emulador FPGA 6502 — Guía de uso

Emulador del computador **fpga-6502-16k** (Sipeed Tang Nano 9K, Gowin GW1NR-9)
para **desarrollar y depurar software** destinado a esa plataforma.

Emula el hardware real de forma fiel: CPU, memoria, UART, SD/SPI, timer, SID y
el Core de Vídeo. Además expone la pantalla y el estado interno por **HTTP**, de
modo que se puede inspeccionar lo que genera un programa sin usar capturas de
pantalla manuales — incluido por un agente de IA.

> **Documentos relacionados**
>
> - [`PROCEDIMIENTOS.md`](PROCEDIMIENTOS.md) — cómo se midió y verificó cada
>   periférico, y las trampas encontradas.
> - [`PUTTY-SETUP.md`](PUTTY-SETUP.md) — configuración del terminal.

---

## Índice

1. [Requisitos](#1-requisitos)
2. [Compilar](#2-compilar)
3. [Ejecutar](#3-ejecutar)
4. [Conectar el terminal](#4-conectar-el-terminal)
5. [Depuración por HTTP](#5-depuración-por-http)
6. [Variables de entorno](#6-variables-de-entorno)
7. [Herramientas de SD](#7-herramientas-de-sd)
8. [Mapa de memoria](#8-mapa-de-memoria)
9. [Cómo emula cada periférico](#9-cómo-emula-cada-periférico)
10. [Depurar un programa paso a paso](#10-depurar-un-programa-paso-a-paso)
11. [Solución de problemas](#11-solución-de-problemas)
12. [Estructura del proyecto](#12-estructura-del-proyecto)

---

## 1. Requisitos

| Componente | Versión | Notas |
|---|---|---|
| MSYS2 UCRT64 | — | Instalado en `C:\msys64` |
| g++ | ≥ 11 | Viene con MSYS2 UCRT64 |
| SDL2 | — | **Opcional pero recomendado** (audio + ventana) |

Instalación de SDL2:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-SDL2
```

El emulador funciona sin SDL2, pero sin ventana ni sonido.

---

## 2. Compilar

```bash
cd /d/Proyectos/EmuladorFpga6502
make -j4
```

Comprobación:

```bash
make info
# SDL2: 1 (1=audio/video habilitados)
# SDL2_PREFIX: C:/msys64/ucrt64
```

Si `SDL2: 0` y tienes el paquete instalado, es que `make` no encuentra la ruta;
el Makefile prueba `/ucrt64`, `C:/msys64/ucrt64` y `cygpath`. Fuerza la ruta con:

```bash
make SDL2_PREFIX=C:/msys64/ucrt64
```

`make` copia `SDL2.dll` junto al ejecutable automáticamente.

Otros objetivos: `make run`, `make clean`, `make rebuild`.

---

## 3. Ejecutar

### Con el lanzador (recomendado)

Hay **dos** lanzadores, según si quieres depuración HTTP o no:

| Script | Depuración HTTP | Cuándo usarlo |
|---|---|---|
| `run.sh` / `run.bat` | **No** | uso normal, jugar |
| `run_debug.sh` / `run_debug.bat` | **Sí** (8080) | depurar, ver pantalla desde el navegador o una IA |

```bash
./run.sh                         # UART 4000, sin depuracion
./run_debug.sh                   # UART 4000, debug en http://localhost:8080

./run.sh 4001                    # otro puerto de UART
./run_debug.sh 4000 otra.img     # otra imagen de SD
./run_debug.sh 4000 tarjeta.img 9000   # debug en el puerto 9000
```

En Windows, `run.bat` y `run_debug.bat` con los mismos argumentos.

El lanzador añade `/ucrt64/bin` al `PATH`, compila si falta el `.exe` o si las
fuentes son más nuevas, y muestra un resumen antes de arrancar.

### Directo

```bash
./emulador.exe <rom.bin> [puerto] [tarjeta.img] [opciones]
```

| Argumento | Descripción | Por defecto |
|---|---|---|
| `rom.bin` | ROM de 16 KB del monitor (obligatorio) | — |
| `puerto` | Puerto TCP del UART | `4000` |
| `tarjeta.img` | Imagen de SD (se crea formateada si no existe) | ninguna (sin SD) |
| `--debug[=PUERTO]` | Servidor de depuración HTTP | **desactivado** (puerto al activarlo: 8080) |

> **Consejo:** pasar `tarjeta.img` es lo normal. Sin ella el monitor responde
> `Error SD: 03`.

The depurador HTTP está **desactivado por defecto**. Se activa cuando se necesita
ver la pantalla o el estado desde fuera:

```bash
./emulador.exe rom/monitor.bin 4000 tarjeta.img --debug        # puerto 8080
./emulador.exe rom/monitor.bin 4000 tarjeta.img --debug=9000   # otro puerto
EMU_DEBUG_PORT=8080 ./emulador.exe rom/monitor.bin 4000 tarjeta.img
```

También se acepta el nombre antiguo `--debug-http[=PUERTO]`.

---

## 4. Conectar el terminal

El UART se expone como **socket TCP crudo**. Se puede usar PuTTY, Tera Term,
`telnet`/`nc` o un script.

### Configuración de PuTTY / ExtraPuTTY

| Ajuste | Valor |
|---|---|
| Host Name | `localhost` |
| Port | `4000` |
| Connection type | **Raw** |
| Terminal-type string | `xterm` |
| **Local echo** | **Force off** |
| **Local line editing** | **Force off** |

`Connection → Data`, `Terminal → Features`. Las dos últimas son **imprescindibles**:
en `Auto`, el terminal añade sus propios saltos de línea y se ven prompts de más.

### Con el hardware real

Es equivalente: `Serial`, `COM5`, `115200`. El emulador reproduce el mismo flujo
de bytes (verificado carácter a carácter).

---

## 5. Depuración por HTTP

Se activa con **`--debug`** (puerto 8080) o **`--debug=PUERTO`**; sin ese flag el
emulador no abre ningún puerto HTTP. Es la herramienta principal para inspeccionar
lo que genera un programa: está pensada para que **una IA (o un script) vea la
pantalla sin intervención humana**.

```bash
./emulador.exe rom/monitor.bin 4000 tarjeta.img --debug
# Debug HTTP: http://localhost:8080  (pantalla, VRAM, estado, RAM)
```

### Rutas

| Ruta | Tipo | Contenido |
|---|---|---|
| `/` | HTML | Índice con vista previa del vídeo (se refresca sola) + panel de estado en vivo |
| `/screen.png` | PNG | Captura exacta de lo que saldría por HDMI (320×240) |
| `/screen.png?scale=2` | PNG | La misma captura ampliada ×2, ×3 o ×4 (útil para leer texto) |
| `/screen.ppm` | PPM | La misma imagen sin comprimir (P6), para herramientas sin decodificador PNG |
| `/vram.txt` | texto | Tilemap, atributos y OAM en hexadecimal + vista ASCII 40×30 |
| `/vram.json` | JSON | Lo mismo que `/vram.txt` pero estructurado, para procesarlo con código |
| `/state.txt` | texto | CPU/UART, LEDs, Core de Vídeo y registros del SID |
| `/ram.bin` | binario | Los 16 KB de RAM tal cual |
| `/ram.bin?start=512&len=256` | binario | Un trozo concreto de la RAM |

### Ejemplos

Vista previa en el navegador (la imagen y el estado se actualizan solos):

```
http://localhost:8080/
```

Guardar una captura desde la línea de comandos:

```bash
curl -o pantalla.png http://localhost:8080/screen.png
curl -o pantalla_grande.png "http://localhost:8080/screen.png?scale=2"
```

Leer el estado del sistema:

```bash
curl http://localhost:8080/state.txt
curl http://localhost:8080/vram.txt
```

Volcar la RAM (por ejemplo la página cero y la pila):

```bash
curl -o zp.bin "http://localhost:8080/ram.bin?start=0&len=256"
```

### Uso desde un agente de IA

Guía práctica para automatizar con el emulador. **El primer punto es el más
importante**: si se ignora, el emulador parece "caerse".

#### 1. Lanzarlo desacoplado (obligatorio)

Un agente suele ejecutar cada comando en una llamada distinta del shell. Si el
emulador se lanza ahí, **el shell lo mata al terminar esa llamada**: el proceso
desaparece sin ningún error y parece un crash.

La forma más simple es usar el script incluido:

```bash
python tools/emu_agent.py start      # arranca desacoplado y espera a que responda
```

Equivale a esto, si se prefiere a mano:

```bash
# MAL: el emulador muere al acabar esta llamada
(./emulador.exe rom/monitor.bin 4000 tarjeta.img --debug &)

# BIEN: proceso independiente que sobrevive
powershell -Command "Start-Process -FilePath '.\emulador.exe' -ArgumentList \
  'rom/monitor.bin','4000','tarjeta.img','--debug' \
  -RedirectStandardOutput '.tmp\emu.log' -RedirectStandardError '.tmp\emu.err' \
  -WindowStyle Normal"
```

En Linux/macOS sería `setsid ./emulador.exe ... > .tmp/emu.log 2>&1 &`.

Opciones útiles:

| Opción | Para qué |
|---|---|
| `--debug` | Activa el HTTP (sin esto no hay capturas) |
| `EMU_AUDIO_DEVICE="..."` | Evita el dispositivo virtual (latencia) |
| `EMU_TRACE=1` | Informa de anomalías de rendimiento |
| `EMU_WAV=ruta.wav` | Graba el SID, para análisis de audio |

#### Herramienta `tools/emu_agent.py`

Envuelve el flujo completo, para no repetir estos detalles en cada sesión:

```bash
python tools/emu_agent.py start                      # arranca y espera
python tools/emu_agent.py load PACMAN.BIN 0800       # carga por el UART
python tools/emu_agent.py run 0800                   # ejecuta
python tools/emu_agent.py screenshot pantalla.png    # captura PNG
python tools/emu_agent.py state                      # /state.txt
python tools/emu_agent.py vram                       # /vram.json
python tools/emu_agent.py ram 0 256                  # RAM en hex
python tools/emu_agent.py status                     # ¿sigue vivo?
python tools/emu_agent.py stop                       # detener
```

`start` sondea hasta que el emulador responde (no duerme un tiempo fijo) y
`status` comprueba si el proceso vive y si el HTTP contesta: las dos cosas que
un agente debe mirar antes de concluir que algo falla.

#### 2. Esperar a que esté listo

No dormir un tiempo fijo: **sondear** hasta que responda.

```python
import time, urllib.request

for _ in range(50):
    try:
        urllib.request.urlopen("http://127.0.0.1:8080/", timeout=1)
        break
    except OSError:
        time.sleep(0.2)
else:
    raise RuntimeError("el emulador no arrancó")
```

#### 3. Conectar al UART y cargar el programa

El UART es un socket TCP crudo en el puerto 4000 (o el que se indique).

```python
import socket, time

s = socket.create_connection(("127.0.0.1", 4000), timeout=5)
time.sleep(0.5)          # da tiempo al monitor a mandar el prompt
s.sendall(b"load JUEGO.BIN 0800\r")
time.sleep(2.5)          # el monitor carga y responde
s.sendall(b"r 0800\r")   # ejecutar
time.sleep(2)
```

> **La dirección importa:** debe coincidir con la de enlace del binario. El SID
> player va en `2700`, no en `0800`. Cargarlo mal no da error: ejecuta basura y
> no imprime nada.

#### 4. Observar el resultado

```python
import urllib.request

png = urllib.request.urlopen("http://127.0.0.1:8080/screen.png").read()
open("pantalla.png", "wb").write(png)   # analizar con PIL
```

| Qué se quiere saber | Dónde |
|---|---|
| Cómo se ve | `/screen.png` (o `?scale=2` para leer texto) |
| Qué debería dibujarse | `/vram.json` (tilemap, atributos, sprites) |
| Si el programa toca el vídeo | `/state.txt` → `tiles escritos` |
| Estado del joystick y LEDs | `/state.txt` → `puerto 1` y `puerto 2` |
| Registros del SID | `/state.txt` → `voz N` y `envolventes` |
| Qué hay en RAM | `/ram.bin?start=0&len=256` |

#### 5. Comprobar que sigue vivo antes de concluir

Si una petición falla, **no asumir un bug**: comprobar el proceso.

```python
import subprocess
alive = "emulador.exe" in subprocess.run(
    ["tasklist", "/FI", "IMAGENAME eq emulador.exe"],
    capture_output=True, text=True).stdout
```

Si no está vivo y se lanzó con `&`, es el cierre del shell. Relanzarlo
desacoplado y repetir.

#### 6. Pararlo al terminar

```bash
taskkill //F //IM emulador.exe
```

#### Errores típicos de un agente

| Error | Consecuencia |
|---|---|
| Lanzarlo con `&` en una llamada del shell | Parece que se cae |
| Usar `sleep` fijo en lugar de sondear | Peticiones que fallan al arrancar |
| Usar `localhost` la primera vez | Espera de hasta 1,6 s (resolución DNS) |
| Olvidar `--debug` | `ConnectionRefused` en el puerto 8080 |
| Cargar en la dirección equivocada | Pantalla en blanco, sin mensaje de error |
| Concluir "va congelado" de una captura | Dos capturas nunca son idénticas (y no deben serlo) |

> **Nota:** el PNG va sin comprimir (deflate "stored") porque no se depende de
> zlib. Pesa ~230 KB, pero se genera en memoria y se envía por tramos, así que
> **no detiene la emulación** mientras tanto.

### Qué contiene `/vram.txt`

```
=== TILEMAP (64 x 32, area 00) ===            mapa completo en hexadecimal
=== ATRIBUTOS (64 x 32, area 01) ===          atributos de cada celda
=== OAM (32 sprites: X, Y, tile, flags) ===   solo los sprites activos, con paleta y flags
=== TILEMAP COMO TEXTO (40x30) ===            zona visible como caracteres
```

### Qué contiene `/vram.json`

```json
{
  "ancho": 64,
  "alto": 32,
  "tilemap":   [[0, 0, 32, ...], ...],
  "atributos": [[0, 1, ...], ...],
  "texto":     ["..................................", ...],
  "sprites":   [{"n": 0, "x": 80, "y": 120, "tile": 3, "flags": 1}, ...]
}
```

### Qué contiene `/state.txt`

```
GPIO:
  LEDs   : 3F  [######]  (bit5 = LEFT ... bit0 = RIGHT, 1 = encendido)
  dir1/dir2: 00 / C0 (1 = entrada)
VIDEO ($D800-$D817):
  tiles escritos: 812 de 2048 celdas
  sprites activos: 12 de 32
  texto visible (24x40 desde fila 0):
    |HOLA MUNDO                              |
    ...
SID ($D400-$D41F):
  voz 1: freq=  1234 ($04D2) pw=  512 ctrl=41 SAW ad=0F1A sr=48
  ...
  filtro: fc=1024 rutas=0F modo=10 volumen=15/15
UART ($C020-$C023):
  STATUS=01 TX_READY
  baud_div=58
```

`tiles escritos` es un indicador rápido de si el programa ha llegado a tocar el
vídeo. Si dice `0 de 2048` y la pantalla es de un color plano, el Core de Vídeo
no ha recibido nada (síntoma típico de un programa que aún no ha arrancado o que
se ha colgado antes de inicializar el vídeo).

### Activar, cambiar de puerto o desactivar

| Forma | Ejemplo |
|---|---|
| Activar (8080) | `./run.sh 4000 tarjeta.img --debug` |
| Otro puerto | `./emulador.exe rom/monitor.bin 4000 tarjeta.img --debug=8081` |
| Variable de entorno | `EMU_DEBUG_PORT=8081 ./run.sh` |
| Desactivado (por defecto) | no pasar nada |
| Desactivarlo | no pasar el flag (es el valor por defecto) |

El servidor escucha solo en `127.0.0.1`: no queda expuesto a la red.

---

## 6. Variables de entorno

| Variable | Efecto | Ejemplo |
|---|---|---|
| `EMU_DEBUG_PORT` | Puerto del servidor de depuración (`0` = desactivar) | `EMU_DEBUG_PORT=9000` |
| `EMU_AUDIO_DEVICE` | Fuerza un dispositivo de audio por nombre | `EMU_AUDIO_DEVICE="Altavoces (Realtek(R) Audio)"` |
| `EMU_WAV` | Vuelca la salida del SID a un WAV | `EMU_WAV=grabacion.wav` |
| `EMU_TRACE` | Muestra avisos de rendimiento y audio | `EMU_TRACE=1` |
| `SDL_AUDIODRIVER` | Fuerza el backend de audio de SDL | `SDL_AUDIODRIVER=directsound` |

### Si el sonido va por detrás del vídeo

Casi siempre es el **dispositivo de audio**, no el emulador. Al arrancar, la
lista de dispositivos marca los que reenvían el audio:

```
audio: 4 dispositivos de salida
audio:   [0] Altavoces (Steam Streaming Speakers)  <- virtual: añade retraso
audio:   [1] Altavoces (Realtek(R) Audio)
audio:   [3] Altavoces (EDIFIER  R19U)
```

Los **virtuales** (Steam Streaming, Nahimic, VB-Audio) codifican o procesan la
señal antes de reproducirla y añaden decenas o cientos de milisegundos. El
emulador no puede compensarlo porque el retraso está fuera de él.

Solución: fijar el dispositivo físico.

```bash
EMU_AUDIO_DEVICE="Altavoces (Realtek(R) Audio)" ./run.sh
```

Lo que sí controla el emulador son unos **~42 ms** (colchón del búfer + período
del callback). Ese valor se ve con `EMU_TRACE=1`.

Ejemplo combinado:

```bash
EMU_WAV=cancion.wav EMU_AUDIO_DEVICE="Altavoces (EDIFIER  R19U)" ./run.sh
```

---

## 7. Herramientas de SD

`tools/sd_tool.py` manipula la imagen MicroFS sin arrancar el emulador.

```bash
python tools/sd_tool.py list    tarjeta.img                     # listar
python tools/sd_tool.py inject  tarjeta.img juego.bin JUEGO.BIN # añadir
python tools/sd_tool.py extract tarjeta.img JUEGO.BIN salida.bin
python tools/sd_tool.py delete  tarjeta.img JUEGO.BIN
python tools/sd_tool.py format  tarjeta.img
```

**Nombres:** máximo 11 caracteres reales (la tabla de 12 incluye el `\0`), y el
monitor los muestra en mayúsculas.

**Otras herramientas:**

| Script | Uso |
|---|---|
| `tools/gen_font.py` | Regenera `src/video/font_c64.h` desde `font_data.vhd` |
| `tools/extract_rom.py` | Extrae la ROM de 16 KB de un `rom.vhd` |
| `tools/diag_rx.cpp` | Diagnóstico del bucle de entrada del monitor (sin red) |

---

## 8. Mapa de memoria

| Rango | Uso |
|---|---|
| `$0000-$3FFF` | RAM 16 KB (stack en `$3E00-$3FFF`) |
| `$8000-$BFFF` | ROM del monitor 16 KB |
| `$FFFA-$FFFF` | Vectores NMI/RESET/IRQ (mapeados dentro de la ROM) |
| `$C000-$C003` | GPIO: puerto 1 (joystick), puerto 2 (6 LEDs) y configs |
| `$C020-$C023` | UART: datos TX/RX, STATUS/CONTROL, baudrate |
| `$C030-$C03F` | Timer / RTC: contadores con latch, timer programable |
| `$C040-$C047` | SPI: datos, estado, CS para la SD |
| `$D400-$D41F` | SID 6581 |
| `$D800-$D817` | Core de Vídeo: puerto indirecto VRAM, scroll, paletas |

Detalle completo en `../fpga-6502-16k/docs/07-MANUAL-PROGRAMACION.md`.

### Registros que más se usan al depurar

**UART (`$C020-$C023`)**

| Dir | Lectura | Escritura |
|---|---|---|
| `$C020` | RX: dato recibido (limpia RX_VALID) | TX: envía un byte |
| `$C021` | STATUS: overrun(4) error(3) busy(2) rx_valid(1) tx_ready(0) | CONTROL: IRQ TX(0) RX(1), reset flags(7) |
| `$C022/$C023` | BAUD bajo/alto | idem |

**SID (`$D400-$D41F`)**

| Dir | Uso |
|---|---|
| `$D400+7·v` | freq (16 bits) de la voz v |
| `$D402+7·v` | ancho de pulso (12 bits) |
| `$D404+7·v` | control: ruido(7) pulso(6) sierra(5) triángulo(4) test(3) anillo(2) sync(1) gate(0) |
| `$D405+7·v` | attack/decay |
| `$D406+7·v` | sustain/release |
| `$D415-$D418` | filtro (corte, rutas, resonancia/modo, volumen) |
| `$D41C` | ENV3 (compatible C64) |
| `$D41D/$D41E` | **ENV1/ENV2** — extensión VU meter de este hardware |
| `$D41F` | **ENV_MAX** — máximo de las tres |

El SID player usa `$D41D-$D41F` para mover los 6 LEDs.

**Vídeo (`$D800-$D817`)**

| Dir | Uso |
|---|---|
| `$D800` | dirección baja de VRAM |
| `$D801` | `area<<6 \| dir[10:8]` (+ bit 5 = plano 1 en patrones) |
| `$D802` | dato; **escribirlo dispara** la escritura |
| `$D803` | STATUS (R): bit7 VBLANK, bit4 VIDEO_READY |
| `$D804-$D807` | scroll X/Y (bajo y alto) |
| `$D813-$D815` | paleta: índice, byte bajo, byte alto (dispara) |

Áreas de VRAM: `$00` tilemap, `$40` atributos, `$80` patrón fondo plano 0,
`$A0` patrón fondo plano 1, `$C0` OAM, `$C8` patrón sprite plano 0,
`$E8` patrón sprite plano 1.

**GPIO (`$C000-$C003`)**

| Dir | Uso |
|---|---|
| `$C000` | puerto 1: joystick (bits 3-7, activo en bajo) |
| `$C001` | puerto 2: 6 LEDs (bits 0-5, activo en bajo) |
| `$C002` | configuración puerto 1 (1 = entrada) |
| `$C003` | configuración puerto 2 (el monitor pone `0xC0`) |

**Timer (`$C030-$C03F`)**

| Dir | Uso |
|---|---|
| `$C030-$C033` | ticks 32 bits (**solo por latch**) |
| `$C038-$C03B` | microsegundos 32 bits (**solo por latch**) |
| `$C03C` | latch: bit0 ticks, bit1 µs, bit6 reset µs, bit7 reset ticks |

---

## 9. Cómo emula cada periférico

### CPU

Tabla de ciclos real por opcode (2–7 ciclos). El bucle principal ejecuta
**3.375 MHz reales** comparando con el reloj de pared, no contando instrucciones.
Esto es lo que hace que el SID suene al tempo correcto y el timer sea fiable.

### UART

Fiel a `uart.vhd`: los bytes del host entran en una FIFO y se publican al CPU
**uno cada ~87 µs** (8N1 a 115200). Importante porque el monitor sondea cuántos
bytes quedan pendientes antes de leer el actual; si se entregan todos de golpe,
el monitor corta el eco.

FIFO con tope de 64 KB: si se llena, se pierde el byte y se marca *overrun*
(igual que el hardware).

### SD / SPI

Protocolo SD completo: CMD0/8/55/41/58/17/24, formato **MicroFS** y persistencia
sector a sector. El fichero de imagen se mantiene **abierto entre escrituras**
(abrirlo y cerrarlo en cada sector bloqueaba el hilo y cortaba el audio).

### Timer

Fiel a `timer_rtc.vhd`. El reloj de sistema es 6.75 MHz y el de CPU 3.375 MHz,
así que **1 µs = 3.5 ciclos de CPU**. Los contadores de ticks y µs corren
siempre; se leen **por latch** (`$C03C`).

Un error típico: leer el contador vivo en vez del latch hace que el SID player
dé tirones cada pocos frames.

### SID

Wrapper sobre **reSIDfp** (el núcleo de VICE, incluido en `third_party/residfp`).
El reloj es **964285 Hz** (27 MHz / 28, derivado en `Board.vhd`), no el del C64.
Se usa remuestreo `RESAMPLE` a 48 kHz.

Las envolventes se leen en `$D41C` (voz 3, compatible C64) y `$D41D-$D41F`
(extensión VU meter del hardware).

#### Sincronía con el vídeo

El audio se genera **antes** de ejecutar la CPU en cada vuelta del bucle, con
los mismos ciclos. Si se genera después, el sonido va siempre un bloque por
detrás de la imagen (un efecto salta en pantalla y suena «un poco después»).

Hay dos ajustes finos que afectan al desfase:

1. **Corrección del resampler.** reSIDfp con reloj 964285,7 Hz y salida a 48 kHz
   entrega **48 053 muestras/s**, un 0,11 % más de las que consume el dispositivo.
   Ese excedente llena el anillo y deja sin datos al audio a ratos. Se compensa
   con `Sid::cycle_correction()` al pedirle ciclos.

2. **Latencia del búfer.** El anillo mantiene un colchón (`AUDIO_TARGET_MS`,
   por defecto 25 ms) más el período del dispositivo (~21 ms con 1024 muestras):
   **~42 ms entre que el juego toca el SID y suena**. No se puede bajar mucho más
   sin que un tropiezo del hilo produzca silencios.

Con `EMU_TRACE=1` el emulador informa de la latencia real:

```
EMU: 3.37 MHz | audio: colchon=21.1 ms + periodo=21.3 ms -> latencia ~42.4 ms | underrun=100 ...
```

`underrun` debe quedarse quieto; si sube, el audio se está quedando sin datos.

### Vídeo

Reproduce el Core de Vídeo: tilemap 64×32, atributos, patrones 2bpp planar,
32 sprites con flip y escala 2×, 3 bandas de scroll y 32 entradas de paleta
RGB444. La fuente C64 se carga al arrancar, como hace el FPGA en el setup.

`tick_frame()` debe llamarse periódicamente: genera VBLANK en `$D803`. Si no se
llama, los juegos que esperan `vc_wait_vblank()` se quedan en bucle.

#### Ritmo de barrido (importante para la velocidad del juego)

El vídeo del FPGA es **858 × 525 píxeles a 27 MHz = 59,94 Hz** (un frame cada
**16,683 ms**). El VBLANK no llena el frame entero: es `v_cnt >= 480`, es decir
**45 líneas de 525 = 8,57 % del frame ≈ 1,43 ms**.

El emulador reproduce esto con un contador de líneas atado al **reloj de pared**:

```
línea actual = us_desde_arranque × 27000 / 858 / 1000
frame         = línea % 525
VBLANK        = frame >= 480
```

Esto importa porque `vc_wait_vblank()` cuenta frames: si el emulador genera el
VBLANK más a menudo que el hardware **el juego entero va acelerado**. No ates el
VBLANK al número de vueltas del bucle principal (da más o menos vueltas según lo
cargado que esté) sino al reloj, como se hace aquí.

Verificado: intervalo medido entre inicios de VBLANK = **16,683 ms (59,94 Hz)**.

> El **timer** (`$C030-$C03F`) es independiente y se avanza con los ciclos de
> CPU reales. Los programas que miden el tiempo con él (como el SID player, que
> usa `$C038-$C03C` a 18320 µs por frame) no se ven afectados por el ritmo del
> vídeo.

### GPIO

Puerto 1 = **joystick Atari (DB9)**, puerto 2 = **6 LEDs** del Tang Nano 9K.

Ambos son activos en bajo y **bidireccionales**, como en el `Board.vhd`: un bit
configurado como salida en `$C002`/`$C003` devuelve el valor escrito; uno
configurado como entrada (`1`) devuelve el pin. Al arrancar todo son salidas,
así que el programa debe poner a entrada los bits del joystick.

#### Mapa del joystick

El joystick ocupa los **5 bits altos** del puerto 1 (los bajos los usa el TM1638
en la placa real):

| Señal | Bit | Máscara | Pin Tang Nano 9K |
|---|---|---|---|
| derecha | 3 | `0x08` | 40 |
| izquierda | 4 | `0x10` | 34 |
| abajo | 5 | `0x20` | 33 |
| arriba | 6 | `0x40` | 30 |
| fuego | 7 | `0x80` | 29 |

En reposo el puerto vale **`0xF8`**; pulsar pone a **0** el bit correspondiente.

Esto coincide con lo que espera el software de la plataforma (ver
`JOYSTICK-INTEGRACION.md` del proyecto Pacman), incluido el aviso de no pisar
los bits 0-2 al escribir `$C002`.

#### Controles de la ventana

| Acción | Teclado | Mando conectado al PC |
|---|---|---|
| Mover | Cursores o **WASD** | Stick izquierdo o cruceta |
| Fuego | **Espacio**, Z, Ctrl izq. o Intro | A, B, X, Y, hombros o gatillos |
| Pantalla completa | **F11** o Alt+Intro | — |

Las dos fuentes **conviven**: si mantienes una dirección con el mando y pulsas
otra tecla, ambas cuentan; soltar una no anula la otra. Cualquier mando que
reconozca SDL sirve (Xbox, DualShock, genéricos), y se puede enchufar con el
emulador en marcha.

#### Comprobar que las pulsaciones llegan

`/state.txt` muestra el puerto 1 tal como lo lee la CPU:

```
  dir1/dir2: 00 / C0 (1 = entrada)
  puerto1: E8  joy=LEFT  (bits 3-7 configurados de entrada)
```

Si pone `NO son entrada: el juego no ha llamado a joy_init()`, el programa no
ha configurado los pines del puerto 1, y leerá siempre el registro de salida
(`0x00`) en vez del joystick. Es el error más común al integrar el mando.

#### Disposición de la ventana

La ventana tiene dos zonas:

```
+---------------------------+-------------------------------+
| PANEL LATERAL             |                               |
|  [ placa Tang Nano 9K ]   |                               |
|  [    6 LEDs          ]   |      vídeo 320x240 x N        |
|  [ terminal del UART  ]   |                               |
+---------------------------+-------------------------------+
```

- **Izquierda (400 px):** el "banco de trabajo" de depuración. Arriba la placa
  con sus 6 LEDs; debajo, el terminal del UART. Está pensado para ir apilando
  **más módulos** (registros del SID, volcado de VRAM, etc.) sin tocar el vídeo.
- **Derecha:** el framebuffer del Core de Vídeo, escalado ×N y alineado arriba.

Si la ventana no cabe en la pantalla (1366×768, portátiles con DPI alto), se
reduce la escala del vídeo a ×2 automáticamente; el panel lateral nunca baja.

#### Pantalla completa

| Acción | Tecla |
|---|---|
| Entrar / salir de pantalla completa | **F11** o **Alt+Intro** |

En pantalla completa **solo se amplía el vídeo**: el panel lateral conserva su
tamaño (400 px) y su posición a la izquierda. El vídeo se escala al **mayor
factor entero** que quepa en el espacio libre (en una pantalla 1080p suele ser
×3 o ×4) y se centra en la zona libre, con 16 px de separación del panel. Si no
cabe a escala entera, se estira manteniendo la proporción 4:3 (sin deformar).

Al salir se restaura la geometría exacta de la ventana (tamaño y posición).

Para arrancar ya en pantalla completa (lanzadores, scripts):

```bash
EMU_FULLSCREEN=1 ./run.sh
```

#### LEDs

Puerto 2, activos en bajo (`1` en `leds()` = encendido). Se dibujan en el panel
lateral, arriba, con forma de placa.

El **SID player** los usa como VU meter: lee las envolventes del SID en `$D41D`
(voz 1), `$D41E` (voz 2), `$D41C` (voz 3) y `$D41F` (máximo de las tres) y
escribe la barra en `$C001`. Puedes verificar ambas cosas en `/state.txt`:

```
  puerto 2 ($C001) - 6 LEDs:
    valor leido: E0  direccion: C0
    LEDs encendidos: 1F  [.#####]  (bit5 = LEFT ... bit0 = RIGHT)
SID ($D400-$D41F):
  envolventes: ENV1=114 ENV2=181 ENV3=  0 MAX=181  ($D41D/$D41E/$D41C/$D41F)
```

#### Terminal del UART en la ventana

En la parte baja del panel lateral, la ventana muestra un **terminal** con la
salida del UART: exactamente lo mismo que saldría por PuTTY. Sirve para depurar
sin cambiar de ventana (cargar un binario, ver un listado, leer mensajes del
programa).

Cómo funciona:

- Es un **tap de solo lectura** sobre el transmisor (`Uart::tap_text()`). No
  consume la FIFO de transmisión, así que **PuTTY sigue recibiendo** los mismos
  bytes si está conectado. Es solo un espejo.
- Los caracteres de control no se pintan: `\r` se descarta y `\n` abre línea.
  Si el programa hace movimiento de cursor (`ESC[...`) no se interpreta; para
  eso está PuTTY.
- Se muestran tantas líneas como quepan (las **últimas**, alineadas abajo): lo
  más reciente está siempre visible, como en un terminal real. Las líneas más
  largas que el panel se cortan por la derecha.
- La fuente es la del Core de Vídeo (`src/video/font_c64.h`), escalada ×2, lo
  que da ~24 columnas de ancho.

Para quitar el panel lateral (ventana solo con vídeo), cambia en `src/main.cpp`:

```cpp
window.start(Video::W, Video::H, 3, /*terminal=*/false);
```

#### Añadir módulos al panel lateral

Todo el panel lo pinta `EmuWindow::present()` en `src/emu_window.cpp`, con un
cursor vertical `y` que va bajando:

```cpp
int y = 12;                 // cursor dentro del panel lateral
// ... módulo 1: placa + LEDs (usa `y`, y `y += alto`)
// ... módulo 2: terminal    (usa `y` y baja hasta el borde inferior)
```

Para añadir un módulo nuevo basta con dibujarlo en el rectángulo
`{8, y, panel_w_ - 16, alto}` y hacer `y += alto + separación`. El terminal
toma lo que sobre hasta el borde inferior, así que conviene poner los módulos
nuevos **antes** de él (arriba) o reducir su alto.

---

## 10. Depurar un programa paso a paso

### Cargar y ejecutar

En el terminal (PuTTY, Raw, `localhost:4000`):

```
load JUEGO.BIN 0800
r 0800
```

**La dirección importa.** Debe coincidir con la de enlace del binario; si no, el
programa ejecuta basura sin avisar. Cómo saberla:

| Programa | Dirección |
|---|---|
| Pacman, Tetris de consola | `0800` |
| **SID player** | **`2700`** (ver `config/programa.cfg` del proyecto) |

Síntoma típico de cargarlo mal: **no aparece nada** por el terminal, el proceso
parece colgado y la pila (`/ram.bin?start=256&len=256`) se llena de bytes de
código repetidos (`A9 3F A9 3F …`).

### Ver qué se está dibujando

Abre `http://localhost:8080/` en el navegador. La vista se actualiza sola.

Desde código o desde una IA, descarga la captura:

```bash
curl -o pantalla.png http://localhost:8080/screen.png
```

### Si la pantalla sale en blanco o azul

El azul uniforme (`RGB(68,136,204)`) **es correcto** cuando no hay programa
cargado: es el color de fondo por defecto de la paleta. Lo que hay que mirar es
si hay programa y sigue azul.

1. Comprueba `http://localhost:8080/vram.txt` o `/state.txt`:
   - **`tiles escritos: 0 de 2048`** → el programa no llegó a escribir la VRAM.
   - **Tilemap con datos pero no se ve** → revisa atributos/patrones.
2. Comprueba que el programa no se quede esperando VBLANK:
   - el emulador genera VBLANK a **59,94 Hz** (16,683 ms) con la duración real
     de 45 líneas; `$D803` bit 4 (VIDEO_READY) está a 1 desde el arranque.
3. Comprueba que cargaste el binario en la dirección correcta (ver arriba).

### Si el juego va demasiado rápido

El programa probablemente cuenta frames esperando VBLANK. Si el emulador
genera más frames de los que toca, todo el juego acelera **de forma uniforme**.

Es un fallo del emulador, no del programa: el VBLANK debe ir atado al reloj de
pared a 59,94 Hz. Ver §9 *Ritmo de barrido*.

### Si el joystick no responde

Mira el puerto 1 en `/state.txt`:

```
  puerto 1 ($C000) - joystick Atari:
    valor leido: F8  direccion: F8
    ...
    pulsado: ninguno  (activo en bajo)
```

- `direccion: 00` + el aviso de `joy_init()` → el programa no configuró los
  bits 3-7 como entrada. En el hardware leería el registro de salida (0).
- `valor leido` cambia al pulsar pero el juego no reacciona → revisa el mapeo
  de bits del programa (derecha=bit3, izquierda=bit4, abajo=bit5, arriba=bit6,
  fuego=bit7).

### Si los LEDs no se encienden

Los LEDs del puerto 2 son **activos en bajo**: escribir `0x00` en `$C001`
enciende los seis. `/state.txt` muestra el registro y la barra:

```
  puerto 2 ($C001) - 6 LEDs:
    valor leido: E0  direccion: C0
    LEDs encendidos: 1F  [.#####]
```

Si `LEDs encendidos` es `00` y el programa escribe en `$C001`, comprueba que no
escribe `0xFF` (todos apagados).

### Si el texto sale raro

Los tiles `$20-$7F` traen la fuente C64 por defecto. Si tu programa define sus
propios patrones, asegúrate de escribir **ambos planos** (`$80` y `$A0`): dejar
solo el plano 0 hace que el color sea el 1 en vez del 3.

### Si el audio no suena

1. Mira la línea de arranque: debe decir `Audio: 48000 Hz, SDL2`.
2. `Audio: sin SDL2` → falta el paquete o la DLL.
3. `WASAPI can't initialize audio client` → el dispositivo por defecto no vale
   (típico con dispositivos virtuales de Steam). Fuerza uno:
   ```bash
   EMU_AUDIO_DEVICE="Altavoces (Realtek(R) Audio)" ./run.sh
   ```
4. Graba a WAV con `EMU_WAV=out.wav` y comprueba si la señal existe.

### Si el SID player no encuentra ficheros

Debe estar compilado con el `romapi.h` **actual** (el de
`monitor_6502_TN-9k_16k/include/romapi.h`). Con la versión vieja,
`rom_mfs_open()` no escribe el puntero en `$F4-$F5` y siempre responde
"Archivo no encontrado". El `SIDPLAY.BIN` de `tarjeta.img` ya está recompilado.

---

## 11. Solución de problemas

| Síntoma | Causa | Solución |
|---|---|---|
| `Uso: emulador.exe <rom.bin>` | Falta el argumento de ROM | `./run.sh` o pasa `rom/monitor.bin` |
| `Error SD: 03` al arrancar | No se pasó imagen de SD | añade `tarjeta.img` como 3.er argumento |
| Prompts `>` duplicados | `Local line editing` en `Auto` | ponlo en **Force off** |
| `[4;1H` literal en pantalla | El programa usa ANSI y el terminal en Raw no lo interpreta | es un programa de consola; usa una sesión que soporte ANSI, o es simplemente su salida |
| No se ve la ventana | SDL2 no disponible | `pacman -S mingw-w64-ucrt-x86_64-SDL2` y recompila |
| Audio entrecortado | Colchón insuficiente o mucha carga | sube `AUDIO_TARGET_MS` en `src/main.cpp` (por defecto 25) |
| **El sonido va por detrás del vídeo** | Dispositivo virtual (Steam, Nahimic) | fija uno físico: `EMU_AUDIO_DEVICE="Altavoces (Realtek(R) Audio)"` |
| El juego va **demasiado rápido** | El VBLANK no sigue el reloj de 59,94 Hz | ver §9 *Ritmo de barrido* |
| El joystick no responde | El programa no llamó a `joy_init()` | `/state.txt` lo dice en el puerto 1 |
| Los LEDs no se encienden | Programa escribe `$C001` con bits a 1 | activos en bajo: 0 = encendido |
| `make` falla con `Permission denied` | El `.exe` está en ejecución | `taskkill //F //IM emulador.exe` |
| El puerto 4000 está ocupado | Otra instancia | `./run.sh 4001` |
| `make` no encuentra SDL2 | Ruta distinta a la esperada | `make SDL2_PREFIX=C:/msys64/ucrt64` |

---

## 12. Estructura del proyecto

```
src/
  main.cpp             bucle principal (3.375 MHz reales), audio, ventana, debug
  cpu_6502.h/.cpp      CPU 6502, 151 opcodes
  cpu_cycles.h         tabla de ciclos por opcode
  bus.h/.cpp           mux de dispositivos + IRQ combinada
  ram_rom.h/.cpp       RAM 16 KB + ROM 16 KB + vectores
  uart.h/.cpp          UART $C020-$C023 (FIFO, entrega a ritmo serial)
  spi_sd.h/.cpp        bridge SPI $C040-$C047
  sd_card.h/.cpp       tarjeta SD + MicroFS
  timer.h/.cpp         timer/RTC $C030-$C03F (con latch)
  sid.h/.cpp           SID sobre reSIDfp $D400-$D41F
  gpio_uart.h/.cpp     GPIO $C000-$C003 (joystick + LEDs)
  audio.h/.cpp         salida de audio SDL2 (ring buffer + colchón)
  emu_window.h/.cpp    ventana: vídeo escalado + panel lateral (LEDs, terminal UART)
  tcp_server.h/.cpp    UART <-> socket TCP
  debug_server.h/.cpp  servidor HTTP de depuración
  debug_dump.h/.cpp    PNG sin dependencias + volcados de texto
  video/
    video.h/.cpp       motor del Core de Vídeo (tiles, sprites, scroll, paletas)
    font_c64.h         fuente C64 generada desde el VHDL
    render.cpp         puente para el renderer

third_party/residfp/   núcleo reSIDfp de VICE (audio)

tools/
  gen_font.py          regenera font_c64.h desde font_data.vhd
  extract_rom.py       extrae la ROM de un rom.vhd
  sd_tool.py           list / info / inject / extract / delete / format
  diag_rx.cpp          diagnóstico del bucle de entrada del monitor

docs/
  GUIA-USO.md          esta guía
  PROCEDIMIENTOS.md    cómo se depuró cada periférico (mediciones y trampas)
  PUTTY-SETUP.md       configuración del terminal
  img/                 capturas

rom/monitor.bin        ROM del monitor v2.6.2 (16 KB)
tarjeta.img            imagen MicroFS de 16 MB

run.sh / run.bat       lanzadores
Makefile               compilación (detecta SDL2 y copia la DLL)
```

---

## Referencias del hardware

| Documento | Ubicación |
|---|---|
| Manual de programación (vídeo, registros) | `../fpga-6502-16k/docs/07-MANUAL-PROGRAMACION.md` |
| UART | `../fpga-6502-16k/src/uart.vhd`, `uart_wrapper.vhd` |
| Timer | `../fpga-6502-16k/src/timer_rtc.vhd` |
| SPI | `../fpga-6502-16k/src/spi_wrapper.vhd` |
| Vídeo | `../fpga-6502-16k/src/hdmi/*.vhd` |
| Source del monitor | `../monitor_6502_TN-9k_16k/` |
| MicroFS | `../microfs-6502-cc65/` |
| Driver SD | `../sdcard-spi-6502-cc65/` |
| SID player | `../Sidplayer_4mon/` |
| Núcleo reSIDfp | `../svn-mirror/vice` |
