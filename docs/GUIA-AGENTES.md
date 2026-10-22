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

### ⚠️ Hay que ejecutarlo FUERA del sandbox

**El emulador no funciona dentro de un sandbox de ejecución.** No es un fallo
del emulador: necesita cosas que un sandbox de terminal normalmente bloquea.

| Necesita | Por qué | Qué pasa en el sandbox |
|---|---|---|
| **Red local** (sockets TCP) | UART en el puerto 4000 y HTTP de depuración en el 8080 | `bind`/`connect` fallan o no se puede conectar desde fuera |
| **Ventana y contexto gráfico** (SDL2) | vídeo, consola y panel | `SDL_CreateWindow` falla o la ventana no aparece |
| **Dispositivo de audio** (WASAPI) | SID | avisos de audio y sin sonido (el emulador sigue) |
| **Proceso persistente** | el emulador corre hasta que se cierra | el shell lo mata al terminar la llamada |
| **Escribir junto al .exe** | `SDL2.dll` y `tarjeta.img` | falla la copia o la apertura de la SD |

**Consecuencia para un agente:** si el entorno de ejecución que usas está
sandeado, lanza el emulador con **permisos fuera del sandbox** (unsandboxed),
y a poder ser **desacoplado** del shell. Si no puedes, no sigas intentándolo:
pidiendo una y otra vez el mismo comando solo se pierde tiempo. Las alternativas
son:

1. Pedir al usuario que lo lance (`run_debug.bat`) y trabajar solo con los
   endpoints HTTP, que sí funcionan desde cualquier sitio.
2. Usar `make test`, que **no** necesita SDL ni red (prueba el intérprete de
   terminal).
3. Usar herramientas puras de CPU, como `tools/diag_boot.cpp`, que se compilan
   y ejecutan sin ventana.

Cómo se pide según la herramienta que estés usando:

| Entorno | Cómo |
|---|---|
| Herramientas tipo Zed / Cursor | marcar la llamada al terminal como **unsandboxed** (o "fuera del sandbox") y dar un motivo: *el emulador necesita red local y ventana* |
| Docker | `--network host` (Linux) o publicar los puertos `-p 4000:4000 -p 8080:8080`; en Windows/macOS la ventana SDL2 no se ve dentro del contenedor |
| WSL | funciona, pero la ventana necesita WSLg; los puertos son locales a WSL salvo que los redirijas |
| Contenedor sin interfaz gráfica | compilar con `make` sin SDL: el emulador arranca **sin ventana** y el UART y el HTTP siguen sirviendo |

> El síntoma típico de ESTAR sandeado: el proceso arranca, escribe las primeras
> líneas y muere; o `curl localhost:8080` da *connection refused* aunque el
> proceso exista.

Para lanzarlo a mano (Windows), **desacoplado**:

```powershell
Start-Process -FilePath '.\emulador.exe' `
  -ArgumentList 'rom\monitor.bin','4000','tarjeta.img','--debug=8080' `
  -WorkingDirectory (Get-Location) `
  -RedirectStandardOutput '.tmp\o.log' `
  -RedirectStandardError  '.tmp\e.log'
```

- **Las redirecciones son obligatorias.** Sin `-RedirectStandardOutput`, el
  proceso hereda la consola del lanzador y la salida puede llenar el pipe y
  **bloquear** el emulador.
- `-WorkingDirectory` evita sustos si el lanzador cambia de carpeta: la ROM y
  `tarjeta.img` se buscan relativas a ella.
- `nohup ... &` **no** sirve en Windows: no existe `nohup` real y el proceso
  queda en el árbol del shell, que se lo lleva por delante al terminar la
  llamada. Por eso hay que usar `Start-Process` o `emu_agent.py start`.

### Cargar un programa por el UART

**El UART acepta UNA sola conexión a la vez.** Mientras hay un cliente
conectado, un segundo `connect()` al 4000 no se atiende (queda en la cola del
`listen` y parece "conectar" sin recibir nada). Abre **una** conexión por
sesión, manda todos los comandos por ella y ciérrala al final.

```python
import socket, time

def sesion(comandos):
    """Abre una conexion, manda los comandos y la cierra limpiamente."""
    s = socket.create_connection(('127.0.0.1', 4000), 3)
    try:
        for cmd, espera in comandos:
            s.sendall(cmd.encode() + b'\r')
            time.sleep(espera)
    finally:
        # Cierre ordenado: manda FIN (recv() del emulador devuelve 0).
        try:
            s.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        s.close()

sesion([('load PACMAN.BIN 0800', 2.0), ('r 0800', 3.0)])
```

La **dirección de carga debe coincidir con la de enlace del binario**. Si no,
el programa ejecuta basura y no avisa.

#### Reconectar por comando es un antipatrón

```python
def enviar(cmd):                       # MAL
    s = socket.create_connection(('127.0.0.1', 4000), 3)
    s.sendall(cmd); time.sleep(0.9); s.close()
```

Cada llamada abre y cierra un socket. Si el cierre es abrupto (sin leer y sin
`shutdown`), Windows envía un **RST**. El emulador lo detecta y libera el
cliente desde `639b92b`, pero **seguir reconectando sigue siendo frágil**: cada
conexión nueva reinicia la sesión del terminal y se pierde la salida intermedia.

#### Si el puerto 4000 deja de aceptar conexiones

Síntoma: `connection refused` aunque el emulador esté vivo (el 8080 responde).

```powershell
netstat -ano | findstr :4000     # busca una linea en CLOSE_WAIT
python tools/emu_agent.py stop
python tools/emu_agent.py start
```

El log del emulador dice el motivo al desconectar:

```
Cliente conectado
Cliente desconectado (conexión perdida (error 10054))   <- RST del cliente
Cliente desconectado (el cliente cerró)                 <- FIN limpio
```

El **error 10054** es `WSAECONNRESET`: el cliente cerró sin leer lo pendiente o
se mató el proceso. No es un fallo del emulador, pero indica que el script
debería cerrar con `shutdown(SHUT_WR)`.

---

## 3. Qué puede leer un agente

| Endpoint | Para qué |
|---|---|
| `/console.txt` | **Lo más útil** para programas de texto: la pantalla de 80×24 en texto plano |
| `/console.png` | Captura de la consola con colores (640×384), `?scale=2..4` |
| `/sdcard/ls` | Archivos de la SD y espacio libre |
| `POST /sdcard/write?name=X.BIN` | Añade un archivo en caliente (cuerpo = datos). Remonta solo |
| `/sdcard/delete?name=X.BIN` | Borra un archivo de la imagen. Remonta solo |
| `/sdcard/rename?name=X.BIN&to=Y.BIN` | Renombra un archivo (conserva los datos). Remonta solo |
| `/sdcard/remount` | Fuerza el re-montaje del monitor (`sd` por el UART) |
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

# Inyectar un binario sin reiniciar el emulador (remonta solo):
curl --data-binary @JUEGO.BIN "http://localhost:8080/sdcard/write?name=JUEGO.BIN"
curl "http://localhost:8080/sdcard/ls"
curl "http://localhost:8080/sdcard/rename?name=JUEGO.BIN&to=JUEGO2.BIN"
curl "http://localhost:8080/sdcard/delete?name=JUEGO2.BIN"

# Forzar el re-montaje (tras tocar la imagen por fuera):
curl "http://localhost:8080/sdcard/remount"
```

`write`, `delete` y `rename` **remontan solos** el directorio del monitor para
que lo vea al instante. Añade `&remount=0` si vas a hacer varias operaciones
seguidas y prefieres remontar una sola vez al final con `/sdcard/remount`.

> **El monitor cachea el directorio**: `/sdcard/ls` siempre muestra la imagen
> real, pero el firmware 6502 (`LS`, `LOAD`, `d`) sigue viendo la lista vieja
> hasta que se re-monta. La ventana re-monta sola cuando es ella la que añade,
> borra o renombra.

> **El emulador debe ejecutarse FUERA de un sandbox**: abre ventana (SDL2) y
> escucha en red (4000/8080). Dentro de un sandbox no funciona; lánzalo en el
> shell normal del usuario.

`/state.txt` incluye también el **flag de usuario** (`$C004 flag usuario: 0/1`),
un bit R/W que sobrevive al reset del sistema y sirve para que el software
recuerde estado entre reinicios.

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
| **Lanzar dentro de un sandbox** | no hay red ni ventana: el emulador no arranca o no responde | ejecutarlo **fuera del sandbox** (ver §2) |
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
