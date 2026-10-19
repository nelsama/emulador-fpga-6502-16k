# Notas de la campana de River Raid

Observaciones recogidas mientras se depuraba `juegos_6502/RiverRaid`.

> **Estado de estas notas: revisadas.**
> Cada afirmación se comprobó contra el código y la ROM. Se indica el resultado:
> ✅ confirmado · ❌ incorrecto (ya no aplica) · ⚠️ matizado.
> El detalle del lado del juego está en `RiverRaid/docs/DIAGNOSTICO-SCROLL.md`.

---

## 1. `rom_get_micros()` devuelve solo 16 bits ✅ CONFIRMADO

**Síntoma:** `rom_get_micros()` (`$BF2D`) devolvía valores del orden de 4·10⁹, sin
relación con el tiempo real.

**Causa real** (desensamblado de la ROM, `$AEC9`):

```asm
AEC9  A9 02      LDA #$02
AECB  8D 3C C0   STA $C03C     ; latch de microsegundos
AECE  AD 3A C0   LDA $C03A     ; byte 2  -> $10
AED1  85 10      STA $10
AED3  AD 3B C0   LDA $C03B     ; byte 3  -> $11
AED6  85 11      STA $11
AED8  AD 38 C0   LDA $C038     ; byte 0  -> A
AEDB  AE 39 C0   LDX $C039     ; byte 1  -> X
AEDE  60         RTS
```

La rutina **devuelve solo 16 bits**: `A` = byte bajo, `X` = byte alto. Los bytes
2 y 3 se guardan en `$10`/`$11` pero **no se devuelven en ningún registro**.

`romapi.h` lo declara como `uint32_t`, así que el compilador toma el valor alto de
lo que haya quedado en `A`/`X` tras la llamada → basura.

**Consecuencia:** la API es inutilizable como reloj de 32 bits, y el
desbordamiento a los 65 536 µs (65 ms) haría inservible incluso la parte baja.

**Solución en el programa:** leer el latch del RTC directamente:

```c
#define TIMER_US_LATCH_CTRL (*(volatile uint8_t *)0xC03C)
#define TIMER_US0           (*(volatile uint8_t *)0xC038)

static uint32_t micros(void) {
    uint32_t v;
    TIMER_US_LATCH_CTRL = 0x02;                /* latch del contador de us */
    v  = (uint32_t)TIMER_US0;
    v |= (uint32_t)(*(volatile uint8_t *)0xC039) << 8;
    v |= (uint32_t)(*(volatile uint8_t *)0xC03A) << 16;
    v |= (uint32_t)(*(volatile uint8_t *)0xC03B) << 24;
    return v;
}
```

Referencia de uso: `RiverRaid/perf.c` (activo con `make PERF=1`).

> **Pendiente de decidir:** esto es un defecto de la *ROM del monitor*, no del
> emulador (el emulador reproduce fielmente lo que hace la ROM). Se podría
> arreglar en `monitor_6502_TN-9k_16k`, devolviendo los 32 bits en `$10-$13` o
> usando una estructura de retorno. Mientras tanto, usar el timer directo.

---

## 2. Presupuesto real por frame ✅ CONFIRMADO

Medido con el latch del RTC en el emulador:

| Dato | Valor medido |
|---|---|
| VBLANK | ~1430 µs (45 de 525 líneas a 27 MHz, `v_cnt >= 480`) |
| Frame | 16683 µs (59,94 Hz) |
| escritura VRAM/OAM (una) | ~7,5 µs |
| `vc_sprite_move` (3 escrituras) | ~14,6 µs |
| `vc_put_cell` + `vc_put_attr` | ~15,2 µs |
| escritura directa a `$D800-$D802` | ~5,7 µs |

Estos valores siguen siendo válidos: el emulador ejecuta la CPU a 3.375 MHz
reales y el VBLANK dura lo mismo que en el FPGA.

---

## 3. Peticiones HTTP: parcialmente incorrecto ❌

**Lo que decía la nota:** que cada petición congelaba el core.

**Comprobado (medido):** el servidor atiende las peticiones **sin detener la
emulación**. Dos capturas seguidas dan imágenes distintas, y `state.txt` cambia
entre peticiones.

| Afirmación | Resultado |
|---|---|
| "Cada petición congela el core" | ❌ **Falso**. La emulación sigue |
| "`?scale=` se ignora" | ❌ **Falso**. Implementado (×1..×4; ×2 da 922 KB) |
| "Capturas seguidas no son el mismo instante" | ✅ **Cierto**, y es lo deseable |

### Las dos causas reales (ya corregidas)

**a) Interbloqueo de cierre.** Tras enviar la respuesta, el servidor esperaba al
FIN del cliente sin haber enviado el propio, y el cliente (curl, urllib) esperaba
a que el servidor cerrara porque se anuncia `Connection: close`. Cada petición
dejaba una conexión colgada hasta 10 s.

*Corregido:* se envía el FIN (`shutdown`) en cuanto termina el envío, y luego se
drena lo que mande el cliente para evitar el RST.

**b) `localhost` resolvía a IPv6.** El servidor escuchaba solo en
`127.0.0.1` (IPv4), pero los clientes resuelven `localhost` primero a `::1`, que
se tragaba la conexión y esperaba timeout antes de reintentar en IPv4.

*Corregido:* dos sockets de escucha, uno en `127.0.0.1` y otro en `::1`.
*(Un único socket IPv6 no sirve: Windows no mapea `127.0.0.1` a
`::ffff:127.0.0.1`.)*

Medido tras los arreglos, con `localhost`:

| Ruta | Tiempo |
|---|---|
| `/state.txt` | 20 ms |
| `/vram.json` | 20 ms |
| `/screen.png` (230 KB) | 20-43 ms |

> **Al medir con Python:** la primera petición de cada proceso tarda ~1,6 s por
> la resolución inicial de `localhost`. Las siguientes van a 20 ms. Usar
> `127.0.0.1` si se quiere evitar incluso eso.

---

## 4. Franjas del tilemap sin actualizar ✅ CONFIRMADO — **corregido**

**Síntoma:** en River Raid se veían franjas con contenido del frame anterior, y
el panel de GAME OVER aparecía duplicado o partido.

**Causa real (más precisa que la de la nota original):** no era que el emulador
no simulara el barrido, sino que **`Video::render()` leía la VRAM viva mientras la
CPU seguía escribiendo en ella**.

El render recorre la pantalla de arriba abajo (~7 ms, con la CPU corriendo en
paralelo a 3.375 MHz). Si el programa escribía a mitad del recorrido:

- las filas ya pintadas usaban el tilemap viejo,
- las de abajo el nuevo,
- y como el scroll también se leía en vivo, la misma fila de mundo se resolvía
  con dos `scroll_y` distintos.

De ahí que el panel apareciera dos veces separado ~180 px, y que en la prueba de
pantalla completa la imagen saliera azul lisa aunque `/vram.json` mostrara los
datos correctos: **el frame no era coherente consigo mismo**.

**Corregido:** `render()` toma ahora una instantánea al entrar (`take_snapshot()`)
con `tilemap_`, `attrib_`, `fgpat_`, `sppat_`, `oam_`, `pal_` y los registros de
scroll/raster, y pinta **exclusivamente** de esa copia.

```cpp
void Video::render(uint32_t* out) {
  take_snapshot();          // una sola lectura consistente
  const Snapshot& s = snap_;
  // ... todo el bucle lee de s, nunca de tilemap_/pal_/oam_
}
```

Coste: despreciable (~16 KB de copia por frame).

**Lo que NO cambia:** el emulador sigue sin simular el barrido línea a línea.
Sigue sin reproducir el *tearing* real del hardware (lo que la CPU escribe tras
el paso del raster se ve en el frame siguiente). Eso es otro trabajo; lo
corregido es la incoherencia **interna** de cada frame.

---

## 5. Defecto de fondo del VHDL ✅ CONFIRMADO

`video_core.vhd` tiene una inconsistencia entre el barrido real (480 líneas) y la
imagen lógica (240):

```vhdl
constant V_VISIBLE : integer := 480;   -- lineas visibles del barrido
constant V_TOTAL   : integer := 525;

yi := to_integer(v_cnt(9 downto 1));   -- 0..479
if yi > 239 then
    y0_log <= to_unsigned(239, 9);     -- satura
else
    y0_log <= to_unsigned(yi, 9);
end if;
```

**Efecto:** de las 480 líneas visibles, las 240 últimas (cuando `yi > 239`)
muestran **la fila 239 repetida**. La imagen se ve repetida hacia abajo.

Tampoco hay margen vertical: `margen` solo mira `h_cnt`, así que la imagen sale
alineada arriba.

**El emulador no reproduce esto:** mapea 1:1 (240 líneas lógicas) y muestra la
imagen correcta. Es decir, **el emulador es "más correcto" que el hardware en
este punto**, pero deja de ser fiel.

> Si se quiere fidelidad exacta con lo que sale por HDMI, habría que replicar el
> defecto. No se hace porque hace la depuración más confusa y el bug está en el
> VHDL, no en los programas.

---

## 6. Azúcar del portapapeles

Ninguna. Lo único relevante para futuras campañas: comprobar con capturas de
"un solo color por fila" (patrón plano) antes de sacar conclusiones sobre
posiciones de tiles.

---

## Resumen para futuras campañas

| Punto | Verdad |
|---|---|
| `rom_get_micros()` | Devuelve 16 bits. Usar el latch del timer directo. **Bug de la ROM**, no del emulador. |
| Velocidad de CPU y frames | Fiel al hardware (3.375 MHz, 59,94 Hz) |
| HTTP | No congela la emulación; `scale` funciona. Latencia real ~20 ms. |
| Franjas mezcladas | **Corregido**: el frame es coherente (instantánea al renderizar). Sigue sin reproducir el *tearing* del hardware. |
| Imagen repetida en HDMI | Defecto del VHDL; el emulador no lo replica |

## Bugs del emulador encontrados en esta revisión

| Bug | Efecto | Estado |
|---|---|---|
| **`render()` leía la VRAM viva** | Un frame mezclaba dos estados: GAME OVER duplicado, franjas, imagen azul con datos correctos | **Corregido**: instantánea al entrar |
| Interbloqueo al cerrar la conexión HTTP | Cada petición tardaba segundos o quedaba colgada | **Corregido** |
| Solo escuchaba en IPv4; `localhost` resuelve a IPv6 | 2 s de espera por petición | **Corregido** |

## Defectos fuera del emulador (no corregidos aquí)

| Defecto | Dónde | Nota |
|---|---|---|
| `rom_get_micros()` devuelve 16 de 32 bits | ROM del monitor | Arreglar en `monitor_6502_TN-9k_16k` |
| Fila 239 repetida en las últimas 240 líneas | `video_core.vhd` | Defecto del VHDL; el emulador no lo replica |

## 7. Segundo bug (sin corregir): la captura mezcla la VRAM de dos instantes

Tras el parche de scroll, probado otra vez con River Raid: **sigue igual y en el
GAME OVER es mas evidente**.

Que se ve (captura de `/screen.png`, 320x240):

- banda de ~40 px arriba con el texto del GAME OVER ("FIRE/R = reintentar",
  "Q = salir") sobre el rio, y una franja verde encima;
- el panel negro del GAME OVER cortado a la mitad, la otra mitad abajo;
- en partida normal, el HUD no cuadra con el cauce.

Un panel escrito en las filas 11-16 del tilemap aparece en DOS zonas separadas
~180 lineas. Un solo buffer con scroll normal no produce eso: la captura esta
mezclando contenido de instantes distintos mientras recorre el mapa.

Sospechas para quien lo arregle (todo en `src/video/video.cpp`, `render()`):

1. **Lectura no atomica de la VRAM**: el tilemap y los atributos se leen celda
   a celda dentro del bucle de pixeles. Si mientras se pinta el CPU escribe
   (o si el hilo del emulador avanza), el mismo frame mezcla dos estados.
   Copiar `tilemap_`, `attrib_` y `oam_` a un buffer local al principio de
   `render()` y pintar desde la copia.
2. **Bandas de raster**: con `raster0_`/`raster1_` cada banda usa scroll y
   region distintas. En River Raid se fijan a `0xFF` (sin bandas), asi que si
   aun asi se cambia de banda, la condicion de cambio esta mal. Recordar que
   en el VHDL la banda MEDIA es la que usa el scroll global
   (`sc_x_in/sc_y_in`) y las bandas 0/2 usan `band2_*`/`band3_*`.
3. **Estado estatico entre frames**: comprobar que el parche de scroll no
   deja acumuladores `static` (linea/linea anterior) que se arrastren de una
   captura a otra: eso explicaria el salto de ~180 lineas (un panel que se
   pinta a mitad de frame se "veria" partido segun cuando llega la peticion).

Forma de comprobarlo sin el juego: pintar un patron de una sola fila
horizontal distinta por cada fila del tilemap (32 colores/filas) y capturar con
el CPU detenido. Si una fila sale dos veces o en posicion equivocada, es
lectura no atomica o mezcla de bandas, no el juego.

Nota de metodo: la captura vigente sale a 320x240 (el `?scale=` sigue sin
efecto) y el menu de GAME OVER solo se pinta una vez, asi que la captura puede
venir de un frame viejo; para diagnosticar hay que parar el core o leer
`/vram.json` y `/screen.png` en la misma peticion.
