# Procedimientos de depuración del emulador

Este documento recoge **cómo se diagnosticó y verificó** cada parte del
emulador: qué se midió, con qué herramienta, qué resultado se esperaba y qué
trampas aparecieron por el camino. No describe qué hace el emulador (eso está en
`GUIA-USO.md`), sino **cómo comprobar que lo hace bien**.

Sirve para dos cosas:

1. Repetir una verificación tras un cambio, sin volver a investigar desde cero.
2. No repetir errores ya cometidos (la sección de trampas es la más útil).

---

## Índice

1. [Principios](#1-principios)
2. [Medir el ritmo de la CPU](#2-medir-el-ritmo-de-la-cpu)
3. [Medir el vídeo (frames)](#3-medir-el-vídeo-frames)
4. [Medir el audio](#4-medir-el-audio)
5. [Verificar el joystick](#5-verificar-el-joystick)
6. [Verificar el SID player](#6-verificar-el-sid-player)
7. [Fidelidad frente al hardware real](#7-fidelidad-frente-al-hardware-real)
8. [Verificar el terminal de consola](#8-verificar-el-terminal-de-consola)
9. [Trampas encontradas](#9-trampas-encontradas)
10. [Plantilla de verificación rápida](#10-plantilla-de-verificación-rápida)

---

## 1. Principios

### Medir, no suponer

Cada vez que se dio por supuesto un valor sin medirlo, resultó estar mal. Todos
los fallos importantes de este proyecto se encontraron midiendo:

| Suposición | Realidad medida |
|---|---|
| "El bucle da 60 Hz" | Daba 118 000 vueltas/s al 100 % de CPU |
| "El SID produce 48 kHz" | Producía 48 053 muestras/s (+0,11 %) |
| "El timer avanza" | No se llamaba nunca a `tick()` |
| "La latencia son 60 ms" | 42 ms reales tras los arreglos |

### Medir desde fuera cuando sea posible

Lo ideal es que la medida no dependa del código que se está probando. Por eso el
servidor HTTP de depuración existe: permite leer el estado y ver la pantalla
desde un proceso independiente.

### El instrumento no debe alterar lo medido

El `printf` en el bucle principal bloquea el hilo (~40 ms con la salida
redirigida) y crea exactamente los tirones que intenta medir. Por eso el
diagnóstico solo imprime **anomalías**, y solo con `EMU_TRACE=1`.

---

## 2. Medir el ritmo de la CPU

**Objetivo:** 3.375 MHz reales (6.75 MHz / 2).

El bucle compara con el reloj de pared, así que el ritmo se comprueba contando
ciclos ejecutados frente al tiempo transcurrido.

### Procedimiento

Arrancar con traza:

```bash
EMU_TRACE=1 ./emulador.exe rom/monitor.bin 4000 tarjeta.img
```

La línea de diagnóstico incluye `3.38 MHz`. Si sale por debajo de ~3.3 MHz, el
hilo no llega.

### Qué mirar además

La traza separa dónde se va el tiempo:

```
EMU: 3.37 MHz | audio: colchon=21.1 ms + periodo=21.3 ms -> latencia ~42.4 ms
    | underrun=100 | bucle=37.0 poll=1.2 cpu=0.3 aud=2.1 dibujo=1.8 hueco=36.7@5.1s
```

| Campo | Significado | Valor sano |
|---|---|---|
| `bucle` | Duración máxima de una vuelta | < 20 ms |
| `hueco` | Tiempo muerto entre vueltas (fuera del proceso) | < 5 ms |
| `cpu` | Tiempo ejecutando la CPU 6502 | ~1-2 ms |
| `aud` | Tiempo generando audio | < 5 ms |
| `dibujo` | Tiempo renderizando y presentando | < 5 ms |

Si `hueco` es grande con `cpu/aud/poll` pequeños, el problema **no está en el
emulador**: el sistema operativo está desalojando el hilo.

---

## 3. Medir el vídeo (frames)

**Objetivo:** 59,94 Hz (858 × 525 a 27 MHz), con VBLANK de 45 líneas.

### Procedimiento

Compilar con la traza de vídeo, que registra el **inicio** de cada VBLANK:

```bash
make clean
make CXXFLAGS_EXTRA=-DEMU_VIDEO_TRACE   # o compilar a mano con ese define
```

Buscar `FRAME` en la salida y calcular la diferencia entre marcas consecutivas:

```python
import re
ms = [float(x) for x in re.findall(r'FRAME ([\d.]+) ms', texto)]
d = sorted(b - a for a, b in zip(ms[1:], ms[2:]))
print(f"mediana {d[len(d)//2]:.3f} ms -> {1000/d[len(d)//2]:.2f} Hz")
```

**Esperado:** mediana 16,683 ms, mínimo ~16,6, máximo ~16,8.

> La traza registra solo **flancos** (de visible a VBLANK). Si registra todas las
> líneas del intervalo salen 45 marcas seguidas separadas por 31,8 µs, que es un
> resultado sin sentido. Ese error ya se cometió una vez.

### Verificar la velocidad del juego (consecuencia)

Un juego que espera VBLANK avanza un paso por frame. Si el emulador genera
frames de más, **todo el juego va acelerado**, y de forma uniforme.

Para medirlo sin tocar el juego: pedir `/screen.png` a intervalos y seguir un
elemento móvil. Con un elemento que se mueve 1 px/frame a 60 fps, debe avanzar
**60 px/s**.

> Cuidado con el amarillo de Pacman: los fantasmas también tienen píxeles
> amarillentos. Filtrar por rango estricto y descartar muestras donde el
> elemento cambie de eje.

---

## 4. Medir el audio

### 4.1 Velocidad de producción (¿va al ritmo real?)

**Objetivo:** el audio generado debe ocupar el mismo tiempo que transcurre.

```bash
EMU_WAV=salida.wav ./emulador.exe ...
```

Con música sonando, medir el crecimiento del fichero:

```python
import os, time
def seg(p): return (os.path.getsize(p) - 44) / 2 / 48000.0   # mono int16 48k
a = seg("salida.wav"); t = time.time()
time.sleep(10)
print((seg("salida.wav") - a) / (time.time() - t))   # debe dar ~1.0000
```

| Resultado | Significado |
|---|---|
| 1,0000 | correcto |
| 0,977 | el SID pierde ciclos (se descartaban bloques) |
| 0,9983 | correcto con el colchón de corrección |

### 4.2 Latencia del bucle de audio

Con `EMU_TRACE=1`:

```
audio: colchon=21.1 ms (max 41.2) + periodo=21.3 ms -> latencia ~42.4 ms
```

- **colchón** = lo que hay en el anillo: lo que el emulador adelanta al vídeo.
- **período** = `samples` del callback de SDL (1024 muestras = 21,3 ms).

La suma es la latencia **del emulador**. La del dispositivo (driver + hardware)
se suma aparte y **no se puede medir desde el emulador**.

### 4.3 Silencios (underruns)

`underrun` cuenta muestras que se sirvieron como silencio por falta de datos.

| Valor | Significado |
|---|---|
| Constante | correcto |
| Sube ~3000 cada 5 s | el audio se queda corto: ~11 ms de silencio por segundo |
| Sube al arrancar y para | normal (el anillo se llena la primera vez) |

Si sube continuamente, la causa típica es **desajuste de frecuencias**: el
generador produce más o menos muestras que las que consume el dispositivo.

**Pero no es la única.** Antes hubo otra que costó encontrar porque el síntoma
no apuntaba al audio: Pac-Man daba **micro-congelamientos** cada pocos segundos.

El bucle reponía el colchón así:

```cpp
const long max_step = audio.frequency() / 1000;   // 1 ms por vuelta
want += std::min(deficit, max_step);
```

El bucle da ~500 vueltas/s (cada una emula ~2 ms de CPU), así que se generaban
como mucho **500 ms de audio por segundo**, la mitad de lo que consume el
dispositivo. El anillo se vaciaba una y otra vez y el sistema se quedaba sin
sonido a trozos.

Cómo se detectó: `EMU_TRACE=1` y mirar `underrun` con el **juego en marcha**.
Con el juego parado el síntoma casi no aparece, lo que despistó al principio.

**Lección:** los `underrun` no siempre son desajuste de frecuencia; también
pueden ser que el generador no dé abasto por un tope artificial. Y conviene
medir con el juego **jugando**, no recién cargado: una pantalla estática esconde
estos fallos.

---

## 5. Verificar el joystick

**El hardware:** joystick Atari en el puerto 1, bits 3-7, activo en bajo.

| Bit | Pin | Señal |
|---|---|---|
| 3 | 40 | derecha |
| 4 | 34 | izquierda |
| 5 | 33 | abajo |
| 6 | 30 | arriba |
| 7 | 29 | fuego |

### Procedimiento

1. Cargar un programa que llame a `joy_init()` (p. ej. Pacman).
2. Abrir `/state.txt` y mirar el puerto 1:

```
  puerto 1 ($C000) - joystick Atari:
    valor leido: F8  direccion: F8
    bit pin senal    modo   nivel
     7   29  fire      entrada 1
     ...
    pulsado: ninguno  (activo en bajo)
```

3. Pulsar una dirección con el teclado o el mando y comprobar que el bit pasa a
   0 y el nombre aparece en `pulsado:`.

### Qué significa cada fallo

| Síntoma | Causa |
|---|---|
| `direccion: 00` y aviso | el programa no llamó a `joy_init()` |
| Bits a 0 con el joystick quieto | polaridad invertida o pines flotando |
| Un bit cambia solo | bit mal mapeado |

> En el hardware real, `$C002` es de byte completo: escribir un valor fijo pisa
> los bits 0-2 (TM1638). El emulador no lo impide: es trabajo del programa hacer
> *read-modify-write*.

---

## 6. Verificar el SID player

**Cuidado con la dirección de carga.** El player está enlazado en **`$2700`**
(ver `config/programa.cfg` del proyecto SID player), no en `$0800`:

```
load SIDPLAY.BIN 2700
r 2700
```

Cargarlo en `$0800` ejecuta basura: los `JSR`/`JMP` absolutos apuntan a
`$27xx-$3Exx`, la pila se desborda y **no aparece ni el banner**.

### Comprobación completa

1. Debe salir el banner:

   ```
   ================================
     SID PLAYER 6502 v1.3.0
   ================================
   Montando SD...OK
   SID (Q=salir, X=XMODEM):
   ```

2. Escribir un nombre de fichero de la SD (p. ej. `AIRWOLF`). Debe mostrar
   título, autor y direcciones de init/play.

3. Mientras suena, en `/state.txt`:

   - los registros del SID cambian (`freq`, `pw`, `ctrl`);
   - las **envolventes** se mueven (`ENV1/ENV2/ENV3/MAX`);
   - los **LEDs** del puerto 2 siguen al máximo de las envolventes (VU meter).

   ```
   envolventes: ENV1=114 ENV2=181 ENV3=  0 MAX=181
   LEDs encendidos: 1F  [.#####]
   ```

---

## 7. Fidelidad frente al hardware real

El emulador se contrasta con el FPGA por el **COM5** usando `pyserial`
(DTR/RTS a `True`), comparando byte a byte lo que responde.

### Regla

Antes de declarar un bug del emulador, comprobar el mismo caso en hardware. En
este proyecto, al menos un "bug" era en realidad una configuración del terminal
(`Local line editing` que añadía saltos de línea).

### Qué comparar

| Aspecto | Cómo |
|---|---|
| UART | Mismos comandos por COM5 y por TCP, comparar la respuesta exacta |
| Vídeo | Captura del monitor HDMI frente a `/screen.png` |
| SID | `EMU_WAV` frente a grabación de línea |
| Timer | Rellena un contador y compáralo con el tiempo de pared |

---

## 8. Verificar el terminal de consola

La consola del panel interpreta ANSI y dibuja CP437. Para comprobar que sigue
haciéndolo bien hay dos caminos.

### Sin abrir la ventana

```bash
make test
```

Cubre: CRLF, `ED`/`EL`/`ECH`, SGR (color, negrita, inverso), cursor
visible/oculto, scroll con historial, CP437, secuencias desconocidas, OSC, TAB,
BS y auto-wrap. Tarda menos de un segundo.

Para interpretar la salida de un binario real:

```bash
./test_terminal.exe ruta/al/juego.bin
```

Vuelca las últimas filas y cuántas celdas tienen byte ≥ 0x80 (los marcos y
bloques). Útil como prueba de humo de un binario nuevo.

### Con el emulador en marcha

```bash
curl http://localhost:8080/console.txt     # 80x24 en texto
curl -o consola.png "http://localhost:8080/console.png?scale=2"
```

Qué mirar, comparando con PuTTY:

| Comprobación | Correcto | Síntoma de fallo |
|---|---|---|
| Marcos `│ ─ ┌┐└┘` | continuos, sin huecos | glifos desplazados o ausentes |
| Colores | texto rojo/verde donde el programa los pide | todo en blanco |
| Posicionamiento | el marcador arriba a la derecha | todo en una columna |
| Limpieza de pantalla (`ESC[2J`) | pantalla nueva vacía | texto viejo solapado |
| Acentos y bloques | `á é í ñ █ ▓ ▒ ░` | huecos o caracteres raros |

El programa de referencia usado fue el Tetris de consola
(`tetris.bin`, 6 KB): al arrancar muestra `SCORE/LINES/LEVEL` arriba a la
derecha, dos carriles con marcos verticales y las fichas como `[]`.

---

### 4.4 Coste del dibujado

El bucle tiene 16,7 ms por fotograma y el emulador da ~500 vueltas/s, así que
cualquier cosa que se haga "cada fotograma" se multiplica por 60. Dos costes
que pasaban desapercibidos:

| Qué | Antes | Ahora |
|---|---|---|
| Consola del panel | ~57.000 rectángulos SDL por fotograma | una textura 640×384 |
| Placa + LEDs + puerto 1 | redibujados 60 veces/s | textura cacheada, solo si cambia |
| Total `present` | 3,17 ms | 1,23 ms |

Cómo medirlo: `EMU_TRACE=1` y el campo `dibujo` del informe cada 5 s. Para
separar el vídeo del panel, comparar con la vista "solo vídeo" (sin panel).

**Regla:** en `present()`, lo que no cambia cada fotograma va a una textura con
caché. Y nada de dibujar píxel a píxel con una llamada SDL por píxel.

---

## 9. Trampas encontradas

Esta sección es la más importante del documento. Cada punto costó tiempo real.

### Lanzar el emulador desde un script

**El emulador muere cuando termina la llamada de terminal que lo lanzó.** Es el
job control del shell matando el grupo de procesos, no un fallo del emulador.
Provoca falsos "crash" tras cada prueba.

Para pruebas que necesiten que sobreviva entre llamadas:

```bash
powershell -Command "Start-Process -FilePath '.\emulador.exe' -ArgumentList \
  'rom/monitor.bin','4000','tarjeta.img','--debug' \
  -RedirectStandardOutput '.tmp\out.log' -RedirectStandardError '.tmp\err.log' \
  -WindowStyle Normal"
```

### `sed` con `\n`

En el shell del proyecto, `sed -i 's/x/y\nz/'` **no expande `\n`**: escribe un
salto de línea literal y rompe el fichero. Usar la herramienta de edición.

### Heredocs de bash con Python

Los heredocs corrompen las secuencias `\n` dentro de scripts Python. Escribir el
script a fichero y luego ejecutarlo.

### Medir con el propio bucle de emulación

Un `printf` en el bucle cuesta ~40 ms y crea los tirones que se quieren medir.
Imprimir solo anomalías y solo con `EMU_TRACE`.

### Comparar bytes con una versión mal cargada

Si un programa no arranca, **primero** comprobar en qué dirección está enlazado.
Un binario cargado en la dirección equivocada falla de forma confusa (pila
desbordada, sin salida por UART) sin relación aparente con la causa.

### Confiar en el nombre "predeterminado" del audio

En sistemas con Steam instalado, el dispositivo predeterminado puede ser
**Steam Streaming Speakers**, un dispositivo virtual que añade un retraso
grande. El emulador lista y marca los virtuales al arrancar; forzar el físico
con `EMU_AUDIO_DEVICE`.

### Suponer que `sleep_until(50 us)` duerme 50 µs

En Windows no: el mínimo es el tick del temporizador. Un bucle que dormía "50 µs"
en realidad giraba 118 000 veces por segundo y saturada un núcleo.

### Creer que un hueco grande es culpa del emulador

Si `cpu`, `aud`, `poll` y `dibujo` suman 5 ms y la vuelta dura 90 ms, el tiempo
se pierde **fuera** del proceso. Mirar saturación de CPU, otros procesos y el
planificador.

### Leer estado vivo en un render que tarda varios milisegundos

`Video::render()` recorre la pantalla de arriba abajo mientras la CPU sigue
ejecutándose. Si lee la VRAM directamente (`tilemap_[cell]`, `pal_[i]`) y el
programa escribe a mitad del recorrido, **el frame mezcla dos estados**: unas
filas viejas y otras nuevas.

Síntomas que provoca, y que se confunden con otros fallos:

- un panel dibujado "dos veces" en zonas separadas (el mismo texto con dos
  scroll distintos);
- texto partido o franjas a media altura;
- una imagen que no coincide con `/vram.json`, aunque los datos guardados sean
  correctos.

**Regla:** todo lo que el render necesite se copia a una instantánea **al
entrar**, y se pinta solo de la copia. Vale para VRAM, paletas, OAM y registros
de scroll/raster. El coste en este proyecto es despreciable (~16 KB por frame).

La alternativa (simular el barrido línea a línea) reproduce incluso el *tearing*
real, pero multiplica el coste y no hace falta para que el frame sea coherente
consigo mismo.

### Confundir el cierre del proceso con un crash del emulador

**El síntoma:** lanzas el emulador desde una llamada de terminal, funciona, y en
la llamada siguiente ya no está. La conexión da `ConnectionResetError` o
`ConnectionRefusedError`, y el log **no contiene ningún error**.

**La causa:** el shell mata el grupo de procesos al terminar la llamada. No es
un fallo del emulador.

**Por qué es tan engañoso:** durante la llamada en que lo lanzas, el proceso
está vivo y responde. Todo parece correcto hasta que vuelves a mirar.

| Forma de lanzarlo | Sobrevive a la llamada |
|---|---|
| `(./emulador.exe ... &)` | **No** |
| `./emulador.exe ... &` (sin paréntesis) | No |
| `Start-Process` (PowerShell) | **Sí** |
| `run.sh` desde una terminal real | Sí |

Verificado en este proyecto: con `( & )`, el proceso está vivo a los 6 s pero
**desaparece en la llamada siguiente**. Con `Start-Process`, sobrevive a cargar
el Pacman y a varias peticiones HTTP.

**Cómo distinguirlo de un crash real:**

| Señal | Cierre del shell | Crash del emulador |
|---|---|---|
| Log de error | vacío | traza, excepción o mensaje |
| Hilo de audio | normal | puede morir en el callback |
| Patrón | muere **entre** llamadas | muere a los N segundos |
| `SDL_AUDIODRIVER=dummy` | no cambia nada | puede evitarlo |

**Regla:** antes de investigar un "crash", lanzarlo desacoplado:

```bash
powershell -Command "Start-Process -FilePath '.\emulador.exe' -ArgumentList \
  'rom/monitor.bin','4000','tarjeta.img','--debug' \
  -RedirectStandardOutput '.tmp\out.log' -RedirectStandardError '.tmp\err.log' \
  -WindowStyle Normal"
```

Solo si **entonces** también muere, hay un bug que investigar.

### Dos diagnósticos erróneos que provocó (para no repetirlos)

Este caso costó dos rondas de investigación equivocada. Los errores concretos:

**Error 1: confundir el patrón con un crash del audio.** Al morir el proceso, y
haber visto que con `SDL_AUDIODRIVER=dummy` otras ejecuciones sobrevivían, se
concluyó que el dispositivo virtual (Steam Streaming Speakers) tumbaba el
emulador. Era una correlación sin relación: en las pruebas con `dummy` el patrón
de arranque que se usó era distinto.

**Error 2: dar por reproducido un bug sin comprobar la versión.** Se comparó el
binario actual con el del commit anterior para decidir si el bug era nuevo, pero
ambos se lanzaron con el mismo patrón defectuoso: los dos "fallaron" y se
concluyó que el bug era antiguo.

**Lo que faltaba en ambos casos:** lanzar desacoplado **primero**. Una vez hecho,
el bug simplemente no existía.

---

## 10. Plantilla de verificación rápida

Tras un cambio importante, esto cubre lo esencial en ~2 minutos.

```bash
# 1. Compilar limpio
make clean && make -j4

# 2. Arrancar con traza y depuracion HTTP
rm -f .tmp/trace.log
powershell -Command "\$env:EMU_TRACE='1'; Start-Process -FilePath '.\emulador.exe' `
  -ArgumentList 'rom/monitor.bin','4000','tarjeta.img','--debug' `
  -RedirectStandardOutput '.tmp\out.log' -RedirectStandardError '.tmp\trace.log' `
  -WindowStyle Normal"

# 3. Comprobar que responden todas las rutas
for p in / /screen.png /vram.txt /vram.json /state.txt \
         "/ram.bin?start=0&len=64" /screen.ppm; do
  curl -s -o /dev/null -w "%{http_code} $p\\n" "http://localhost:8080$p"
done

# 4. Cargar el Pacman y mirar la captura + estado
#    (en el terminal UART: load PACMAN.BIN 0800 / r 0800)
curl -o pac.png http://localhost:8080/screen.png
curl -s http://localhost:8080/state.txt | head -30
```

### Lista de comprobación

- [ ] Compila sin errores ni warnings
- [ ] Arranca y muestra la ventana
- [ ] `Audio: 48000 Hz, SDL2 (periodo ...)` y ningún aviso de dispositivo virtual
- [ ] Con `run_debug.sh` (o `--debug`), todas las rutas HTTP devuelven 200
- [ ] Con `run.sh` (sin flag), **no** se abre el puerto 8080
- [ ] El Pacman se ve en `/screen.png` (laberinto, fantasmas, HUD)
- [ ] `tiles escritos` > 0 y `sprites activos` > 0
- [ ] El joystick responde con el teclado (`pulsado: LEFT`, etc.)
- [ ] Sin `underrun` creciendo en la traza
- [ ] `bucle` y `hueco` por debajo de 20 ms
- [ ] El SID player (en `$2700`) muestra el banner y suena
