# Comentarios sobre la documentación del core de vídeo

**Desde:** proyecto EmuladorFpga6502
**Para:** proyecto fpga-6502-16k
**Fecha:** 2026-10-27
**Referencia:** `fb1c5bc`, `895cb67`

## Por qué este informe

Al implementar el emulador los últimos detalles del core (`SOLID_HIT`, patrón
de paleta saturado, patrones de sprite 0-2, setup `$D816`) hubo que leer el RTL
línea a línea. En ese proceso aparecieron varios **comentarios que no describen
lo que el código hace**. Ninguno es un fallo de diseño: la lógica está bien y
los dos hallazgos reales (`pal_ptr`, patrones 0-2) ya están corregidos.

Lo que sigue es documentación. Van ordenados por lo que más confunde a quien
lee el core por primera vez, que suele ser alguien escribiendo software.

---

## 1. El comentario del barrido de colisión describe otro algoritmo

**Prioridad: alta** (induce a error al programar)

`video_core.vhd:1015`:

```vhdl
-- BARRI DO DE COLISION SPRITE<->TILE SOLIDO (punto = centro)
--   Para cada sprite (0..31):
--     centro = (sx + 4, sy + 4)  (y el bit 4 del atributo = SOLIDO)
--     celda = (centro_x/8) + (centro_y/8)*64
```

Y `video_core.vhd:381`:

```vhdl
-- COLISION SPRITE <-> TILE SOLIDO (punto = centro del sprite)
```

Pero el código (líneas 1068-1099) **no usa el centro**: usa el byte +4 del OAM
(`COLL_POINT`) y, si `SCALE2X` está activo, lo escala:

```vhdl
sdx := unsigned(ocp(2 downto 0));
sdy := unsigned(ocp(5 downto 3));
if osp2 = '1' then
    if sdx = 7 then pdx := to_unsigned(15, 4);
    else            pdx := resize(sdx(2 downto 0) & '0', 4); end if;
    ...
```

El centro solo es el caso `COLL_POINT = $24`. El manual del programador
(`07-MANUAL-PROGRAMACION.md` §3.3) **sí lo documenta bien**; el desfase está solo
en los comentarios del RTL.

**Sugerencia:** cambiar `(punto = centro)` por `(punto = COLL_POINT, byte +4 del
OAM; el centro es COLL_POINT=$24)`. De paso, `BARRI DO` → `BARRIDO`.

---

## 2. `clear_stats` está declarado y nunca se usa: los flags no son sticky

**Prioridad: alta** (cambia el comportamiento que documenta el manual)

`video_core.vhd:82` declara:

```vhdl
clear_stats : in  std_logic;   -- 1 = limpiar flags sticky (al leer)
```

`video_bus.vhd` genera el pulso al leer `$D803`
(`clear_tgl` → `clear_stats`, líneas 156-166 y 421-427). Pero **`clear_stats` no
vuelve a aparecer en el cuerpo de `video_core.vhd`**: `grep -c` da **1**
ocurrencia (la declaración).

Y `video_core.vhd:410-412` dice:

```vhdl
--   OVERFLOW  : 1 si hubo mas sprites que huecos en el line buffer (sticky)
--   HIT       : 1 si dos sprites solaparon en pantalla (sticky)
--   Los bits sticky se limpian al leer el registro (gestionado en video_bus).
```

**Lo que ocurre de verdad:**

| Bit | Comportamiento real |
|---|---|
| 5 `SOLID_HIT` | Se **recalcula entero cada frame** en el blanking (`coll_flag` se limpia al empezar el barrido) |
| 6 `OVERFLOW` | Se mantiene mientras dure la condición; no acumula tras leer |

Es decir, **no hay flags sticky**. Leer `$D803` no limpia nada porque no hay nada
que limpiar.

**Por qué importa:** un programa que asuma el comportamiento documentado
(«leo una vez por frame y el flag se queda hasta que lo leo») funciona igual,
pero uno que **acumule** eventos entre lecturas no verá lo que espera. En el
emulador tuve que decidir qué implementar y me guié por el código, no por el
comentario.

**Sugerencia:** o se conecta `clear_stats` a los registros (y se vuelve sticky de
verdad), o se corrige el comentario a «se recalculan cada frame; no son sticky».
La segunda opción es menos trabajo y describe el hardware actual.

---

## 3. `Y >= 248` desactiva el dibujado, pero no la colisión

**Prioridad: media** (sutil; cuesta depurar)

Está documentado como «sprite deshabilitado», y es cierto **solo para el
dibujado**. El barrido de colisión recorre los 32 sprites sin filtrar por Y
(`video_core.vhd:1055-1100`), así que un sprite con `Y = 248` **puede activar
`SOLID_HIT`**.

En el emulador lo implementé ignorando `Y >= 248`, y fue lo correcto: mis
primeros tests fallaban justo por asumir lo contrario (ponía los sprites
«deshabilitados» en `Y = 248` y seguía habiendo colisión; el punto caía en la
**fila 31**, que nadie espera).

**Sugerencia:** una nota de una línea en §6.1 junto a «Y ≥ 248 = deshabilitado»:
*«solo afecta al dibujado; el barrido de colisión recorre los 32 sprites»*.

---

## 4. La colisión puede desbordar a la celda 0

**Prioridad: media**

El punto de colisión se calcula con **X de 9 bits y Y de 8**, y las sumas
desbordan y vuelven a empezar. Verificado con el mismo cálculo del RTL:

| Sprite | `COLL_POINT` | Punto | Celda |
|---|---|---|---|
| `Y=248` | `$24` (4,4) | (4, 252) | fila **31** |
| `Y=255` | `$27` (7,7) | (7, **6**) | fila **0** ← desborda |
| `Y=240` | 2× `$3F` (15,15) | (15, 255) | fila 31 |

La fila de `Y=255 + 7` da `262 & 0xFF = 6`, que es la **fila 0**. Un sprite que
sale por abajo puede colisionar con una celda sólida de la fila superior.

Es coherente con el hardware (los contadores tienen ese ancho y envolver es lo
que hace la aritmética modular de VHDL), pero no es lo que un programador
espera. Conviene avisarlo en el manual.

**Sugerencia:** nota en §3.3: *«X es de 9 bits y Y de 8: un sprite fuera de
pantalla puede envolver y colisionar con una celda del lado opuesto»*.

---

## 5. Faltan dos tablas derivadas del código

**Prioridad: media**

Ambas se pueden deducir leyendo el RTL, pero son las que más tiempo cuestan:

### 5.1 `COLL_POINT` y su escalado

| Punto | `COLL_POINT` | Con `SCALE2X` |
|---|---|---|
| centro | `$24` | `$24` → punto `(8,8)` |
| pie | `$3C` | `$3C` → punto `(8,15)` |
| borde derecho | `$27` | `$27` → punto `(15,8)` |
| esquina sup-izq | `$00` | `$00` → `(0,0)` |

El detalle que no es obvio: **`7` pasa a `15`, no a `14`** (`if sdx = 7 then
pdx := 15`). Es correcto para que el borde del sprite a 2× caiga en su píxel 15,
pero se lee raro y conviene explicarlo.

### 5.2 Coordenadas sin recortar a la pantalla

El punto se calcula en el dominio de la pantalla y **luego** se toma la celda,
sin comprobar que esté dentro de los 320×240 visibles. Un sprite en `X = 316`
con `dx = 7` da punto 323 → celda 40, que está fuera de la pantalla (40
columnas: 0-39) pero **es una celda válida del mapa**. Si el juego dibujó algo
sólido ahí, el sprite «colisiona» fuera de pantalla.

**Sugerencia:** avisar de que la colisión no está limitada al área visible.

---

## 6. Detalles menores de comentarios

| Fichero:línea | Dice | Debería decir |
|---|---|---|
| `video_core.vhd:713` | `-- fase 2: patrones de tiles 0..3 (en blanco)` | Son **0..7** (8 tiles × 8 filas = 64, como el propio contador `init_cnt = 63` delata) |
| `video_vram.vhd:7-8` | `tilemap: 2048 x 8 (1200 usadas)` | **2048 celdas**: el mapa es 64×32 y el scroll puede mostrar cualquiera. El 1200 es la pantalla (40×30), no lo usado |
| `video_core.vhd:1015` | `BARRI DO` | `BARRIDO` |

---

## 7. Lo que está bien y conviene no tocar

Para que quede claro que esto es una lista de documentación, no de defectos:

- **Formato de patrones** (2bpp planar, bit 7 a la izquierda) — impecable, y el
  emulador lo reproduce sin ambigüedad.
- **§6.0 del manual** tras `895cb67` — la aclaración de que los patrones 0-2 son
  sobrescribibles es exactamente lo que faltaba. Con ella se programó bien a la
  primera.
- **§1.9 sobre overscan** (`08-TIPS`) — explica un «en la placa no se ve» muy
  habitual. Añadiría una referencia cruzada desde el manual §6.
- **El orden del init** (fases 0-4 y qué banco toca cada una) — correcto y bien
  delimitado; de ahí salió que la fase 4 no toca sprites.

---

## Resumen

| # | Fichero | Cambio | Coste |
|---|---|---|---|
| 1 | `video_core.vhd:381,1015` | Comentario: el punto es `COLL_POINT`, no el centro. `BARRI DO` | 4 líneas |
| 2 | `video_core.vhd:410-412` / `:82` | Documentar que no hay flags sticky (o implementarlo) | 2 líneas / trabajo real |
| 3 | `07-MANUAL` §6.1 | Nota: `Y >= 248` no excluye del barrido de colisión | 1 línea |
| 4 | `07-MANUAL` §3.3 | Nota: X de 9 bits y Y de 8 desbordan (Y=255+7 → fila 0) | 2 líneas |
| 5 | `07-MANUAL` §3.3 | Tabla de `COLL_POINT` con `SCALE2X` (7 → 15) | ~10 líneas |
| 6 | varios | Erratas menores (tiles 0-7, 2048 celdas, `BARRIDO`) | 3 líneas |

Nada de esto bloquea: el emulador funciona con el comportamiento del **código**.
El problema es que un programador que lea solo los comentarios implementará otra
cosa y la verá fallar en la placa.
