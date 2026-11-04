# Restricciones de uso del emulador (software)

Reglas que debe cumplir **cualquier programa que vaya a ejecutarse en el
emulador**, y motivos concretos. Están sacadas del código del emulador, no de
suposiciones.

> Esto **no** es una recopilación de requisitos de cada juego (teclas, niveles,
> gráficos). Aquí solo se documenta lo que rompe la **depuración** o la
> comunicación con el emulador cuando el software no lo tiene en cuenta.
>
> Si el problema es que el servidor HTTP deja de contestar con el proceso vivo,
> eso es un fallo **del emulador**, no del software: ver
> [`PROCEDIMIENTOS.md` §8ter](PROCEDIMIENTOS.md#8ter-servidor-http-de-depuración-rst-al-cerrar-la-conexión).

---

## 1. El programa NO puede leer el UART y a la vez olvidarse del monitor

**Restricción.** Un programa que arranca desde el monitor hereda el canal de
entrada del UART. Si deja de leerlo, el emulador **no puede volver a hablarle**:

- El comando `Q` (volver al monitor) queda sin atender.
- Los comandos inyectados por `curl "http://localhost:8080/uart?cmd=..."`,
  PuTTY o `emu_agent.py` se acumulan en la FIFO de recepción sin que nadie los
  lea.

**Qué se ve.** El programa funciona, pero no responde a nada desde fuera. Al
cabo de un tiempo el UART marca *overrun* y los bytes se descartan (la FIFO tiene
un tope de 64 KB).

**Qué hacer.**

- Durante el desarrollo, salir con un reset: el botón **`RESET`** del panel de la
  ventana (acción `reset_cpu`). Vacía la FIFO de entrada para que lo pendiente
  no llegue al programa siguiente.
- Si el programa es un juego sin teclado, no esperes a que devuelva el control
  al monitor: el reset es la única salida. Por eso el botón existe.
- **No hay endpoint HTTP para el reset.** Desde un script o un agente no se
  puede reiniciar la máquina: `/uart?cmd=Q` tampoco sirve si el programa ya no
  lee el UART (el `Q` se queda en la FIFO). Solo `/uart?cmd=...` funciona
  mientras el monitor siga al mando.

**Por qué es así.** Es fiel al hardware: la salida del programa no reinicia la
CPU, y `Q` es un comando que atiende el **monitor**, no el sistema.

---

## 2. Los comandos al monitor llevan su tiempo

**Restricción.** Tras un reset, el monitor tarda en inicializar la SD y montar
MicroFS (~0,3 s), y un `LOAD` de un binario de 14 KB tarda otros tantos en
leerse por SPI.

**Qué se ve.** Si mandas `LOAD JUEGO.BIN 0800` y `R 0800` en el mismo instante,
el `R` llega antes de que el monitor esté listo y se pierde: el juego no arranca
y parece que «el emulador no hace nada».

**Qué hacer.**

- Desde la ventana, el botón **`RUN`** ya lo resuelve: lanza el `LOAD` y deja el
  `R` en cola 800 ms (ver `comando_diferido` en `src/main.cpp`).
- Desde un script, espera entre los dos comandos (2-3 s es suficiente) en vez de
  encadenarlos.

---

## 3. El teclado de la ventana NO llega al programa si está en modo `JOY`

**Restricción.** El selector del panel manda el teclado al **UART** o al
**joystick**, no a los dos.

**Qué se ve.** Un programa que lee WASD por el UART no responde al teclado, y en
el `/console.txt` no aparece nada.

**Qué hacer.** Poner el selector en `UART`. El mando físico funciona en los dos
modos.

---

## 4. La dirección de carga debe coincidir con la de enlace

**Restricción.** El monitor escribe el binario en la dirección que le pidas, sin
comprobarla.

**Qué se ve.** El programa arranca y ejecuta basura, sin ningún error.

**Qué hacer.** Usar la dirección de enlace del binario (`0800` en los juegos
normales; el SID player va en `2700`). Si no la conoces, no la adivines.

---

## 5. Los nombres de archivo son 8.3

**Restricción.** MicroFS guarda nombres de 8 caracteres de base + 3 de
extensión.

**Qué se ve.** Un archivo añadido como `river_raid_deluxe.bin` aparece en la SD
como `RIVER_RA.BIN`, y el `LOAD` con el nombre largo no lo encuentra.

**Qué hacer.** Usar el nombre **tal y como aparece en `ls`** (el recorte lo hace
el emulador; el monitor solo conoce el corto).

---

## 6. El sondeo del HTTP y las teclas van al MISMO sitio

**Restricción.** Todo lo que entra por el UART —el teclado de la ventana, PuTTY,
`/uart?cmd=`, el socket 4000 y el sondeo del HTTP— acaba en la **misma FIFO de
recepción**. El programa no puede distinguir de dónde vino.

**Qué se ve.** Un juego que lee el UART interpreta como teclas cualquier cosa
que llegue ahí. Es un caso real: Space Invaders lee `a`, `d`, `q` y espacio; si
un agente sondea `/` o manda `/uart?cmd=...` mientras se juega, el juego **se
mueve solo o dispara solo**, y no hay forma de saber por qué mirando la
pantalla.

**Qué hacer.**

- No sondear el HTTP mientras se prueba un juego que lee el UART: cerrar la
  pestaña del índice o no lanzar `emu_agent.py` en bucle. Una petición a
  `/screen.png` no escribe en el UART, pero `/uart?cmd=` sí.
- Si hace falta depurar por HTTP **y** jugar a la vez, el programa debe
  descartar la entrada que no espere (teclas válidas solamente), o el agente
  debe usar solo las rutas de lectura (`/screen.png`, `/vram.txt`,
  `/state.txt`, `/console.txt`).
