# Notas de la campana de River Raid

Observaciones sobre el emulador hechas mientras se depuraba
`juegos_6502/RiverRaid`. Es solo documentacion: no se ha tocado ni un archivo
de codigo del emulador. El detalle del lado del juego esta en
`RiverRaid/docs/DIAGNOSTICO-SCROLL.md`.

## 1. Medir rendimiento del CPU

`rom_get_micros()` ($BF2D) **no devuelve microsegundos** en este emulador
(valores del orden de 4e9, sin relacion con el tiempo real). Para medir el
coste de una rutina hay que leer el latch del RTC del core:

```c
#define TIMER_US_LATCH_CTRL (*(volatile uint8_t *)0xC03C)
#define TIMER_US0           (*(volatile uint8_t *)0xC038)

static uint32_t micros(void) {
    uint32_t v;
    TIMER_US_LATCH_CTRL = 0x02;              /* latch del contador de us */
    v  = (uint32_t)TIMER_US0;
    v |= (uint32_t)(*(volatile uint8_t *)0xC039) << 8;
    v |= (uint32_t)(*(volatile uint8_t *)0xC03A) << 16;
    v |= (uint32_t)(*(volatile uint8_t *)0xC03B) << 24;
    return v;
}
```

Referencia de uso: `RiverRaid/perf.c` (activo con `make PERF=1`).

## 2. Presupuesto real por frame

| Dato | Valor medido |
|---|---|
| VBLANK | ~1430 us (45 de 525 lineas a 27 MHz, `v_cnt >= 480`) |
| Frame | 16683 us (59,94 Hz) |
| escritura VRAM/OAM (una) | ~7,5 us |
| `vc_sprite_move` (3 escrituras) | ~14,6 us |
| `vc_put_cell` + `vc_put_attr` | ~15,2 us |
| escritura directa a `$D800-$D802` | ~5,7 us |

## 3. Las peticiones HTTP congelan el core

Cada peticion a `/screen.png` o `/vram.json` se atiende con el core parado
(comprobado con Arkanoid: dos peticiones a 0,5 s devolvian los mismos sprites
byte a byte). Por tanto:

- No sirve medir "ritmo" con peticiones sueltas: parece congelado.
- Una captura y un `/vram.json` leidos seguidos **no son el mismo instante**.
- `?scale=` en `/screen.png` se ignora: **la captura siempre sale a 320x240**,
  aunque la vista HTML pida `scale=2`.
- `urlretrieve`/`curl` seguidos pueden devolver imagenes de instantes distintos.

## 4. Franzido: franjas del tilemap sin actualizar

En River Raid se vieron franjas con contenido del frame anterior al regenerar
una fila del tilemap. Descartado:

- Escritura fuera de VBLANK: el juego gasta ~7 ms por frame (muy por encima de
  los 1,43 ms del VBLANK) y **el demo oficial de scroll
  (`demo_scroll_v64.asm`) tiene el mismo defecto**, asi que no es un mal uso
  de la API.
- Lentitud de la memoria de patrones: los patrones se cargan una vez al
  arrancar y se ven bien.

Sospecha viva: el emulador reconstruye el video en una sola pasada al pedir la
captura, sin simular el barrido linea a linea con la VRAM de cada instante. En
hardware, lo que el CPU escribe despues de que el raster paso por esa zona no
se ve hasta el frame siguiente, y eso produce justo esas franjas mezcladas.

## 5. Defecto de fondo del VHDL que conviene tener presente

`video_core.vhd` trabaja con `V_VISIBLE = 480`, `V_TOTAL = 525` y
`row_t1 <= y0_world(2 downto 0)`, mientras `y0_log` satura a 239: 480 de las
525 lineas muestran la ULTIMA fila de la imagen logica, o sea que la imagen
sale repetida hacia abajo. Tampoco hay margen vertical (`margen` solo mira
`h_cnt`). El emulador mapea 1:1 (240 lineas) y no reproduce ese efecto.

## 6. Azucar del portapapeles

Ninguna. Lo unico relevante para futuras campañas: comprobar con capturas de
"un solo color por fila" (patron plano) antes de sacar conclusiones sobre
posiciones de tiles.
