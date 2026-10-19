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

## 4. Franjas del tilemap sin actualizar ✅ CONFIRMADO (por diseño)

**Síntoma:** en River Raid se veían franjas con contenido del frame anterior.

**Causa:** el emulador **renderiza el frame completo en una sola pasada**, con el
estado de la VRAM en ese instante (`Video::render()`). No simula el barrido
línea a línea.

En el hardware real el raster recorre la pantalla mientras la CPU escribe. Lo que
la CPU escribe **después** de que el raster pasó por esa zona no se ve hasta el
frame siguiente, y eso produce exactamente esas franjas mezcladas.

**No es un error del programa:** el juego gasta ~7 ms por frame (muy por encima
de los 1,43 ms de VBLANK) y el propio `demo_scroll_v64.asm` tiene el mismo
efecto.

**Es una limitación conocida del emulador.** Reproducirlo exigiría renderizar por
líneas con "instantáneas" de la VRAM, lo que multiplicaría el coste. Si en el
futuro se necesita para depurar *tearing*, la vía sería guardar una copia de la
VRAM por línea de barrido.

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
| Franjas mezcladas | Esperado: el emulador no simula el barrido línea a línea |
| Imagen repetida en HDMI | Defecto del VHDL; el emulador no lo replica |

## Bugs del emulador encontrados en esta revisión

| Bug | Efecto | Estado |
|---|---|---|
| Interbloqueo al cerrar la conexión HTTP | Cada petición tardaba segundos o quedaba colgada | **Corregido** |
| Solo escuchaba en IPv4; `localhost` resuelve a IPv6 | 2 s de espera por petición | **Corregido** |

## Defectos fuera del emulador (no corregidos aquí)

| Defecto | Dónde | Nota |
|---|---|---|
| `rom_get_micros()` devuelve 16 de 32 bits | ROM del monitor | Arreglar en `monitor_6502_TN-9k_16k` |
| Fila 239 repetida en las últimas 240 líneas | `video_core.vhd` | Defecto del VHDL; el emulador no lo replica |
