// Pruebas del Core de Vídeo: divergencias detectadas contra el RTL.
//
//   ./test_video.exe   (make test)
//
// Comprueba comportamientos que el hardware tiene y que, si el emulador hace
// distintos, producen el clásico "en el emulador se ve bien y en la placa no":
// estado inicial de la VRAM, saturación del puntero de paleta y setup.

#include <cstdio>

#include "video/video.h"

static int fallos = 0;

static void check(bool ok, const char* que) {
  if (ok) return;
  std::printf("  FALLO: %s\n", que);
  fallos++;
}

// El puntero de paleta SATURA en 31 (video_bus.vhd: "if pal_ptr_r /= 11111").
//
// Antes envolvía a 0: la entrada 31 era inalcanzable y la escritura siguiente
// pisaba pal_bg(0) == BG_COLOR, el color de fondo global. Un programa que
// escribía las 32 entradas veía cambiar el fondo, cosa que en la placa no pasa.
static void test_paleta_satura() {
  Video v;
  bool resp = false;

  v.write(0xD813, 0, resp); // pal_ptr = 0
  for (int i = 0; i < 32; i++) {
    v.write(0xD814, (uint8_t)i, resp);            // color[7:0]
    v.write(0xD815, (uint8_t)(i & 0x0F), resp);   // color[11:8] + escribe + ptr++
  }
  check(v.palette_ptr() == 31, "paleta: el puntero se queda en 31");

  // Las entradas 0..31 tienen su valor.
  check(v.palette()[0] == 0x000, "paleta: entrada 0");
  check(v.palette()[31] == ((15u << 8) | 31u), "paleta: entrada 31 escrita");

  // Escribir más sigue yendo a la 31 (no envuelve a 0 ni pisa BG_COLOR).
  const uint16_t bg_antes = v.palette()[15];
  for (int i = 0; i < 5; i++) {
    v.write(0xD814, (uint8_t)(0xF0 + i), resp);
    v.write(0xD815, 0x0F, resp);
  }
  check(v.palette_ptr() == 31, "paleta: sigue saturado en 31");
  check(v.palette()[15] == bg_antes, "paleta: BG_COLOR (entrada 15) intacta");
  check(v.palette()[31] == ((15u << 8) | 0xF4), "paleta: la 31 se reescribe sola");
}

// Los patrones de sprite 0..2 los escribe la fase 3 del init (cara, rombo, X);
// el setup ($D816) los deja a cero. Nunca contienen lo que cargue el programa.
static void test_patrones_demo() {
  Video v;
  bool resp = false;
  const auto& sp = v.sprite_patterns();

  // Fila 0 del patrón 0 = 0x3C (cara), plano 0.
  check(sp[0 * 16 + 0] == 0x3C, "patrones: patrón 0 es la cara");
  check(sp[1 * 16 + 0] == 0x18, "patrones: patrón 1 es el rombo");
  check(sp[2 * 16 + 0] == 0x81, "patrones: patrón 2 es la X");
  // Los patrones 3..63 deben estar libres (a cero) tras el arranque.
  bool libres = true;
  for (int patron = 3; patron < 64; patron++)
    for (int i = 0; i < 16; i++)
      if (sp[patron * 16 + i] != 0) libres = false;
  check(libres, "patrones: 3..63 libres al arrancar");

  // Los tiles de FONDO 0..7 también traen arte de demo (fase 2): no arrancan
  // a cero. Se comprueba que hay tinta y que son 8 distintos.
  {
    const auto& bg0 = v.bg_patterns();
    int con_tinta = 0;
    for (int t = 0; t < 8; t++) {
      bool tinta = false;
      for (int i = 0; i < 16; i++)
        if (bg0[t * 16 + i] != 0) tinta = true;
      if (tinta) con_tinta++;
    }
    check(con_tinta == 8, "patrones: los 8 tiles de fondo de demo traen arte");
    // El 8 en adelante no es arte de demo (salvo la fuente, que empieza en $20).
    bool vacio8 = true;
    for (int i = 0; i < 16; i++)
      if (bg0[8 * 16 + i] != 0) vacio8 = false;
    check(vacio8, "patrones: el tile de fondo 8 arranca a cero");
  }

  // Escribir algo en la VRAM ANTES del setup, para ver que se borra.
  v.write(0xD800, 0x05, resp);  // dir lo
  v.write(0xD801, 0x00, resp);  // area 00 = tilemap
  v.write(0xD802, 0x42, resp);
  check(v.tilemap()[5] == 0x42, "setup: escritura previa aplicada");

  // $D816 solo dispara: BUSY=1 y la escritura siguiente del CPU se IGNORA,
  // como en el hardware (init_done='0').
  v.write(0xD816, 1, resp);
  check((v.read(0xD803, resp) & 0x10) == 0, "setup: VIDEO_READY a 0 durante BUSY");
  check(v.read(0xD817, resp) == 0x01, "setup: BUSY activo tras $D816");
  v.write(0xD800, 0x07, resp);
  v.write(0xD801, 0x00, resp);
  v.write(0xD802, 0x99, resp);
  check(v.tilemap()[7] == 0x20, "setup: escritura durante BUSY se ignora");

  // Al terminar el BUSY (tick_frame) se aplica: todo a cero.
  for (int i = 0; i < Video::SETUP_LINEAS - 1; i++) v.tick_frame();
  check(v.read(0xD817, resp) == 0x01, "setup: BUSY dura ~184 us");
  v.tick_frame();
  check(v.read(0xD817, resp) == 0x10, "setup: VIDEO_READY al terminar");
  check(v.tilemap()[5] == 0x00, "setup: tilemap a cero tras el setup");
  check(v.tilemap()[7] == 0x00, "setup: la escritura bloqueada no reaparece");

  // La fuente ASCII SÍ se re-expande (el setup no la deja en blanco).
  const auto& bg = v.bg_patterns();
  bool fuente = false;
  for (int i = 0; i < 16; i++)
    if (bg[0x41 * 16 + i] != 0) fuente = true; // 'A'
  check(fuente, "setup: la fuente se re-expande");

  // Y el setup deja a cero el arte de demo de fondo 0..7, igual que el de
  // sprite 0..2 (el core recorre las mismas fases escribiendo ceros).
  bool tiles_cero = true;
  for (int t = 0; t < 8; t++)
    for (int i = 0; i < 16; i++)
      if (bg[t * 16 + i] != 0) tiles_cero = false;
  check(tiles_cero, "setup: los tiles de fondo 0..7 quedan a cero");

  bool sp_cero = true;
  for (int patron = 0; patron <= 2; patron++)
    for (int i = 0; i < 16; i++)
      if (v.sprite_patterns()[patron * 16 + i] != 0) sp_cero = false;
  check(sp_cero, "setup: los patrones de sprite 0..2 quedan a cero");
}

// El barrido mantiene VIDEO_READY y alterna VBLANK.
static void test_barrido() {
  Video v;
  bool resp = false;
  bool vblank = false, visible = false;
  for (int i = 0; i < Video::H_LINES * 2; i++) {
    v.tick_frame();
    const uint8_t st = v.read(0xD803, resp);
    if (st & 0x80) vblank = true; else visible = true;
    if (!(st & 0x10)) { check(false, "barrido: VIDEO_READY siempre a 1"); break; }
  }
  check(vblank, "barrido: aparece VBLANK");
  check(visible, "barrido: aparece zona visible");
}

// Estado inicial del ARRANQUE: el OAM está a cero, así que el sprite 0 queda
// en (0,0) con el patrón 0 (la cara) y el bit Y<248 lo da por activo.
//
// Es fiel al core: oam_reg arranca a cero (video_core.vhd:311) y el criterio
// de descarte es sy < 248. Por eso al encender se ve una cara arriba a la
// izquierda, en la placa y en el emulador. Si algún día se "limpia" el OAM al
// arrancar, esto deja de coincidir con el hardware.
static void test_arranque_muestra_cara() {
  Video v;
  const auto& oam = v.oam();
  bool todo_cero = true;
  for (uint8_t b : oam)
    if (b != 0) todo_cero = false;
  check(todo_cero, "arranque: el OAM empieza a cero");

  // Y=0 < 248 => el sprite 0 está habilitado.
  check(oam[1] < 248, "arranque: el sprite 0 está habilitado");
  // Y su patrón (0) es la cara, no un patrón vacío.
  check(v.sprite_patterns()[0 * 16 + 0] == 0x3C, "arranque: el patrón 0 es la cara");
}

// Escribe un sprite completo en el OAM por el puerto indirecto.
static void poner_sprite(Video& v, int n, int x, int y, int tile, int flags,
                         int control) {
  bool resp = false;
  const int base = n * 5;
  const uint8_t campos[5] = {(uint8_t)(x & 0xFF), (uint8_t)y, (uint8_t)tile,
                             (uint8_t)flags, (uint8_t)control};
  for (int k = 0; k < 5; k++) {
    v.write(0xD800, (uint8_t)(base + k), resp);
    v.write(0xD801, 0xC0, resp); // area 11 + OAM (bit3=0)
    v.write(0xD802, campos[k], resp);
  }
}

// Marca (o desmarca) una celda como sólida escribiendo su atributo.
static void poner_solido(Video& v, int col, int fila, bool solido) {
  bool resp = false;
  const int celda = fila * Video::MAP_W + col;
  v.write(0xD800, (uint8_t)(celda & 0xFF), resp);
  v.write(0xD801, (uint8_t)(0x40 | ((celda >> 8) & 0x07)), resp); // area 01
  v.write(0xD802, (uint8_t)(solido ? 0x14 : 0x04), resp);         // bit4 = SOLIDO
}

// Deshabilita los 32 sprites.
static void limpiar_oam(Video& v) {
  for (int i = 0; i < 32; i++) poner_sprite(v, i, 0, 248, 0, 0, 0x24);
}

// Deja SOLO el sprite 0 donde importa y aparta los otros 31.
//
// Y >= 248 desactiva el DIBUJADO, pero el barrido de colisión recorre los 32
// sprites igual (el RTL no filtra por Y). Por eso los sobrantes se llevan a la
// celda (38,0) con COLL_POINT 0, lejos de las celdas que usan los tests.
//
// Ojo con la X: es de 9 bits y el byte 8 va en FLAGS bit 2 (X_bit8). Poner 304
// sin ese bit deja X = 48 (304 & 0xFF), que es otra celda.
static void solo_sprite_0(Video& v) {
  limpiar_oam(v);
  const int x = 38 * 8; // celda 38
  const int flags_x8 = (x > 255) ? 0x04 : 0x00;
  for (int i = 1; i < 32; i++)
    poner_sprite(v, i, x & 0xFF, 0, 0, flags_x8, 0x00);
}

// Deja la VRAM de atributos a cero para que un bloque no arrastre las celdas
// sólidas que marcó el anterior.
static void limpiar_atributos(Video& v) {
  bool resp = false;
  for (int celda = 0; celda < 2048; celda++) {
    v.write(0xD800, (uint8_t)(celda & 0xFF), resp);
    v.write(0xD801, (uint8_t)(0x40 | ((celda >> 8) & 0x07)), resp);
    v.write(0xD802, 0x00, resp);
  }
}

// Avanza hasta ENTRAR en el blanking, que es cuando se hace el barrido.
// Garantiza cruzar un flanco: si ya estamos en blanking, sale primero a la
// zona visible para que el siguiente tick_frame().
static void entrar_en_blanking(Video& v) {
  bool resp = false;
  for (int i = 0; i < Video::H_LINES * 4; i++) {
    const bool antes = (v.read(0xD803, resp) & 0x80) != 0;
    v.tick_frame();
    const bool ahora = (v.read(0xD803, resp) & 0x80) != 0;
    if (ahora && !antes) return; // flanco de entrada
  }
}

static bool solid_hit(const Video& v) {
  bool resp = false;
  return (const_cast<Video&>(v).read(0xD803, resp) & 0x20) != 0;
}

// SOLID_HIT ($D803 bit 5): colisión sprite <-> tile sólido.
//
// El hardware recorre los 32 sprites en el blanking y marca el bit si el
// COLL_POINT de alguno cae sobre una celda con el bit 4 del atributo.
static void test_solid_hit() {
  Video v;
  limpiar_oam(v);
  entrar_en_blanking(v);
  check(!solid_hit(v), "solid: sin sprites sobre sólidos, bit a 0");

  // Sprite en (0,0) con COLL_POINT centro (4,4) -> celda (0,0).
  poner_sprite(v, 0, 0, 0, 0, 0x00, 0x24);
  entrar_en_blanking(v);
  check(!solid_hit(v), "solid: celda no sólida -> 0");

  poner_solido(v, 0, 0, true);
  entrar_en_blanking(v);
  check(solid_hit(v), "solid: celda sólida -> 1");

  // El bit se recalcula entero cada frame, no es sticky.
  poner_solido(v, 0, 0, false);
  entrar_en_blanking(v);
  check(!solid_hit(v), "solid: se limpia al dejar de tocar");

  // Un sprite DESHABILITADO para el DIBUJADO (Y >= 248) SÍ participa en el
  // barrido de colisión: el RTL recorre los 32 sin filtrar por Y. Con Y=248 y
  // centro (4,4) el punto cae en (4,252) -> celda (0,31) = 1984.
  limpiar_oam(v);
  poner_solido(v, 0, 0, false);
  poner_solido(v, 0, 31, true);
  entrar_en_blanking(v);
  check(solid_hit(v), "solid: Y>=248 no excluye del barrido");

  // COLL_POINT sirve para elegir el punto: con el sprite en (0,0) y el punto
  // en (7,7) cae en la celda (0,0) igual; desplazándolo a X=8 ya va a la 1.
  solo_sprite_0(v);
  poner_solido(v, 0, 0, false);
  poner_solido(v, 1, 0, true);
  poner_sprite(v, 0, 0, 0, 0, 0x00, 0x00); // punto en (0,0) -> celda 0
  entrar_en_blanking(v);
  check(!solid_hit(v), "solid: COLL_POINT (0,0) no llega a la celda 1");
  poner_sprite(v, 0, 0, 0, 0, 0x00, 0x07); // punto en (7,0) -> sigue celda 0
  entrar_en_blanking(v);
  check(!solid_hit(v), "solid: COLL_POINT (7,0) tampoco");
  poner_sprite(v, 0, 1, 0, 0, 0x00, 0x27); // dx=7,dy=4 -> x=8 -> celda 1
  entrar_en_blanking(v);
  check(solid_hit(v), "solid: desplazar el sprite cambia de celda");

  // SCALE2X escala el punto de colisión: dx=4 pasa a 8.
  solo_sprite_0(v);
  limpiar_atributos(v);
  poner_solido(v, 0, 0, true);
  poner_sprite(v, 0, 0, 0, 0, 0x10, 0x24); // 2x, dx=4 -> 8 -> celda 1
  entrar_en_blanking(v);
  check(!solid_hit(v), "solid: SCALE2X duplica el COLL_POINT (celda 1)");
  poner_sprite(v, 0, 0, 0, 0, 0x00, 0x24); // 1x, dx=4 -> celda 0
  entrar_en_blanking(v);
  check(solid_hit(v), "solid: sin 2x el mismo punto cae en la celda 0");
}

// El barrido de colisión pasa el punto a coordenadas de MUNDO sumando el
// scroll (cc645d3), para leer la misma celda que se ve bajo el sprite.
static void test_solid_hit_scroll() {
  Video v;
  bool resp = false;

  // Con scroll 0 el sprite en (0,0) toca la celda (0,0).
  solo_sprite_0(v);
  limpiar_atributos(v);
  poner_solido(v, 0, 0, true);
  poner_sprite(v, 0, 0, 0, 0, 0x00, 0x00);
  entrar_en_blanking(v);
  check(solid_hit(v), "scroll: sin scroll, toca la celda 0");

  // Con scroll_x = 8 el mismo sprite mira la celda (1,0), así que la 0 ya no
  // cuenta. Se usan 8 px = una celda exacta para que no haya ambigüedad.
  v.write(0xD804, 8, resp); // scroll_x lo
  v.write(0xD805, 0, resp); // scroll_x hi
  entrar_en_blanking(v);
  check(!solid_hit(v), "scroll: con scroll_x=8 la celda 0 deja de contar");

  poner_solido(v, 0, 0, false);
  poner_solido(v, 1, 0, true);
  entrar_en_blanking(v);
  check(solid_hit(v), "scroll: con scroll_x=8 toca la celda 1");

  // Scroll vertical: 8 px baja una fila. El sprite 0 usa COLL_POINT $00, así
  // que su punto es (0,0); con scroll_y=8 pasa a (0,8) -> columna 0, fila 1,
  // que es la celda 64 (no la 65: la columna no se mueve).
  v.write(0xD804, 0, resp);
  v.write(0xD806, 8, resp); // scroll_y lo
  v.write(0xD807, 0, resp);
  entrar_en_blanking(v);
  check(!solid_hit(v), "scroll: scroll_y=8 ya no mira la fila 0");
  poner_solido(v, 1, 0, false);
  poner_solido(v, 0, 1, true);
  entrar_en_blanking(v);
  check(solid_hit(v), "scroll: scroll_y=8 mira la fila 1");
}

// Con split de raster, el barrido usa el scroll de la ÚLTIMA banda DIBUJADA,
// no el de la superior. Es una limitación del propio core (la señal scroll_x
// es única y se reescribe al cruzar cada banda), y el emulador la reproduce.
static void test_solid_hit_banda() {
  Video v;
  bool resp = false;

  // Prep: sprite 0 en (0,0) con COLL_POINT (0,0) -> punto (0,0).
  solo_sprite_0(v);
  limpiar_atributos(v);
  poner_sprite(v, 0, 0, 0, 0, 0x00, 0x00);

  // Split SIN banda inferior: superior (band2) a 8, normal a 0.
  v.write(0xD809, 100, resp); // raster0: fin de la banda superior
  v.write(0xD80A, 8, resp);   // band2_x = 8
  v.write(0xD80B, 0, resp);
  v.write(0xD80E, 0xFF, resp); // raster1 = $FF: sin banda inferior
  // La última banda dibujada es la MEDIA, que usa el scroll NORMAL (0).
  poner_solido(v, 0, 0, true);
  poner_solido(v, 1, 0, true);
  entrar_en_blanking(v);
  check(solid_hit(v), "banda: sin banda inferior usa el scroll normal");

  // Ahora con banda inferior a 16: la última dibujada es la INFERIOR.
  v.write(0xD80E, 200, resp);  // raster1
  v.write(0xD80F, 16, resp);   // band3_x = 16 -> celda 2
  v.write(0xD810, 0, resp);
  limpiar_atributos(v);
  poner_solido(v, 0, 0, true); // scroll normal
  poner_solido(v, 1, 0, true); // scroll band2
  poner_solido(v, 2, 0, true); // scroll band3
  entrar_en_blanking(v);
  check(solid_hit(v), "banda: con banda inferior, sprite 0 mira la celda 2");

  // Si solo la celda de la banda SUPERIOR es sólida, NO debe detectar:
  // el hardware está mirando el scroll de la INFERIOR.
  limpiar_atributos(v);
  poner_solido(v, 1, 0, true);
  entrar_en_blanking(v);
  check(!solid_hit(v), "banda: la superior no se usa en el barrido");
}

// El scroll vertical del DIBUJADO y el de la COLISIÓN deben usar el mismo
// ancho (11 bits, como y0_world del RTL). Envolver a 8 bits antes de dividir
// da el mismo resultado porque 256 = 8 x 32, pero tener dos anchos distintos
// para la misma operación solo invita a equivocarse al leer el código.
//
// Se comprueba dibujando con scroll_y alto: el frame no debe cambiar respecto
// a dibujar con su equivalente módulo 256.
static void test_scroll_vertical_ancho() {
  Video a, b;
  bool resp = false;

  // Tilemap con una fila distinta en cada fila de celda, para que un desfase
  // de fila se note. Se escribe en LOS DOS videos: si solo se pusiera en uno,
  // la comparación de frames no probaría nada (fallaba por el tilemap, no por
  // el scroll).
  for (Video* v : {&a, &b}) {
    for (int celda = 0; celda < 2048; celda++) {
      v->write(0xD800, (uint8_t)(celda & 0xFF), resp);
      v->write(0xD801, (uint8_t)(0x00 | ((celda >> 8) & 0x07)), resp); // area 00
      v->write(0xD802, (uint8_t)((celda / Video::MAP_W) & 0xFF), resp);
    }
  }

  // a) scroll_y = 260 ; b) scroll_y = 4 (260 & 0xFF). Deben dar el mismo frame:
  // 260 = 32 tiles x 8 px + 4, y el mapa tiene 32 filas, así que la fila de
  // celda coincide y el píxel dentro del tile también.
  a.write(0xD806, 260 & 0xFF, resp);
  a.write(0xD807, 260 >> 8, resp);
  b.write(0xD806, 4, resp);
  b.write(0xD807, 0, resp);

  const size_t n = (size_t)Video::W * Video::H;
  std::vector<uint32_t> fa(n), fb(n);
  a.render(fa.data());
  b.render(fb.data());
  check(fa == fb, "scroll_y: 260 y 4 dibujan lo mismo");

  // Y con 2047 (máximo del registro): equivale a 2047 & 0xFF = 255, porque
  // 2047 = 31*64 + 63 y 255 = 31*8 + 7 -> misma fila (31) y mismo píxel (7).
  a.write(0xD806, 2047 & 0xFF, resp);
  a.write(0xD807, 2047 >> 8, resp);
  b.write(0xD806, 255, resp);
  b.write(0xD807, 0, resp);
  std::vector<uint32_t> fc(n), fd(n);
  a.render(fc.data());
  b.render(fd.data());
  check(fc == fd, "scroll_y: 2047 y 255 dibujan lo mismo");
}

// El hardware resuelve el solapamiento con un LINE BUFFER de 12 entradas
// (video_core.vhd, NSL = 12), no dibujando los sprites en orden:
//   - gana el sprite OPACO de MENOR INDICE que cubre el píxel;
//   - a partir del 13º sprite que cruza una línea, el resto no se dibuja.
static void test_sprites_line_buffer() {
  Video v;
  bool resp = false;
  std::vector<uint32_t> frame((size_t)Video::W * Video::H, 0);

  // Patrón de prueba: el tile 1 con todos los píxeles encendidos (color 3).
  // Formato de la VRAM de patrones de sprite: area 11 (HI=$C0), bit3=1 y
  // direccion d = patron*8 + fila; el plano va en el bit 5 del registro alto.
  auto patron_lleno = [&](int patron) {
    for (int fila = 0; fila < 8; fila++) {
      for (int plano = 0; plano < 2; plano++) {
        v.write(0xD800, (uint8_t)(patron * 8 + fila), resp);
        v.write(0xD801, (uint8_t)(0xC0 | 0x08 | (plano ? 0x20 : 0x00)), resp);
        v.write(0xD802, 0xFF, resp);
      }
    }
  };
  patron_lleno(1);

  // Paleta de sprite: entrada 16+p = colores de la paleta 0 de sprite. Se deja
  // la de por defecto; basta con comparar el píxel entre dos renders.

  limpiar_atributos(v);
  limpiar_oam(v);

  // Dos sprites de 8x8 solapados en el mismo píxel (0,0).
  // El sprite 0 va a 2x (16x16) para cubrir más y el 1 a 1x.
  const int x0 = 0, y0 = 0;
  poner_sprite(v, 0, x0, y0, 1, 0x00, 0x00);  // índice menor, tile 1
  poner_sprite(v, 1, x0, y0, 1, 0x00, 0x00);  // índice mayor, mismo sitio
  v.render(frame.data());
  const uint32_t con_dos = frame[0];

  // Con solo el sprite 1 el píxel debe salir IDÉNTICO: el sprite 0 es el que
  // gana (mismo tile, así que el color coincide; lo que importa es que el 1 no
  // se pinte encima, que antes lo hacía con otro tile/color).
  limpiar_oam(v);
  poner_sprite(v, 1, x0, y0, 1, 0x00, 0x00);
  v.render(frame.data());
  check(frame[0] == con_dos, "sprites: el índice menor manda (el 1 no tapa al 0)");

  // Y lo que NO debe pasar: que el sprite de índice mayor tape al menor.
  // Se comprueba con tiles distintos: 0 lleno y 1 distinto (vacío).
  limpiar_oam(v);
  poner_sprite(v, 0, x0, y0, 1, 0x00, 0x00);  // lleno
  poner_sprite(v, 1, x0, y0, 0, 0x00, 0x00);  // vacío (transparente)
  v.render(frame.data());
  const uint32_t solo_0 = frame[0];
  limpiar_oam(v);
  poner_sprite(v, 0, x0, y0, 1, 0x00, 0x00);
  v.render(frame.data());
  check(frame[0] == solo_0,
        "sprites: uno transparente de índice mayor no borra al de índice menor");
}

// Avanza hasta la línea FÍSICA `linea` (0..524). El barrido arranca en 480.
static void ir_a_linea(Video& v, int linea) {
  entrar_en_blanking(v); // línea 480
  for (int l = 480; l != linea; l = (l + 1) % Video::H_LINES) v.tick_frame();
}

// Casos 1-11 del manual del core (apéndice B.6).
static void test_casos_manual() {
  Video v;
  bool resp = false;
  std::vector<uint32_t> f((size_t)Video::W * Video::H, 0);
  const uint32_t BG = 0xFF4488CC, C1 = 0xFFFF8800, C2 = 0xFF0000FF;
  const uint32_t FONDO = 0xFF0000AA; // paleta 0, color 1
  auto escribir = [&](int area, int d, int dato) {
    v.write(0xD800, (uint8_t)d, resp);
    v.write(0xD801, (uint8_t)(area | (d >> 8)), resp);
    v.write(0xD802, (uint8_t)dato, resp);
  };
  // Patrón 1: color 1 entero. Patrón 2: solo la mitad izquierda. 3: vacío.
  for (int fila = 0; fila < 8; fila++) {
    escribir(0xC8, 8 + fila, 0xFF);  escribir(0xE8, 8 + fila, 0);
    escribir(0xC8, 16 + fila, 0xF0); escribir(0xE8, 16 + fila, 0);
    escribir(0x80, 8 + fila, 0xFF);  escribir(0xA0, 8 + fila, 0); // tile 1
  }
  auto px = [&](int x, int y) { return f[(size_t)y * Video::W + x]; };
  auto pintar = [&] { v.render(f.data()); };
  auto fila_de = [&](int n, int y) {
    limpiar_oam(v);
    for (int i = 0; i < n; i++) poner_sprite(v, i, i * 10, y, 1, 0, 0);
  };
  auto ovf = [&] { return (v.read(0xD803, resp) & 0x40) != 0; };
  auto vistos = [&](int desde, int hasta, int n) {
    for (int i = 0; i < n; i++)
      if ((px(i * 10, 100) == C1) != (i >= desde && i <= hasta)) return false;
    return true;
  };

  fila_de(13, 100); pintar();
  check(vistos(0, 11, 13), "B.6-1: 13 en línea, se ven 0-11");
  poner_sprite(v, 0, 0, 248, 1, 0, 0); pintar();
  check(vistos(1, 12, 13), "B.6-2: Y=248 no ocupa hueco");
  poner_sprite(v, 0, 400, 100, 1, 0x04, 0); pintar();
  check(vistos(1, 11, 13), "B.6-3: fuera de pantalla en X ocupa hueco");

  limpiar_oam(v);
  poner_sprite(v, 0, 8, 8, 2, 0, 0);
  poner_sprite(v, 1, 8, 8, 1, 1, 0); pintar();
  check(px(8, 8) == C1 && px(15, 8) == C2, "B.6-4: transparencia entre sprites");
  poner_sprite(v, 1, 8, 8, 3, 1, 0); pintar();
  check(px(15, 8) == BG, "B.6-4: ninguno opaco deja ver BG_COLOR");

  limpiar_oam(v);
  poner_sprite(v, 0, 8, 8, 1, 0x20, 0);
  poner_sprite(v, 1, 8, 8, 1, 1, 0);
  escribir(0x00, 64 + 1, 1); pintar();
  check(px(8, 8) == FONDO, "B.6-5: PRIO=1 sobre fondo opaco deja el fondo");
  // Aunque el fondo opaco sea del mismo color que BG_COLOR.
  v.write(0xD813, 1, resp); v.write(0xD814, 0x8C, resp); v.write(0xD815, 4, resp);
  pintar();
  check(px(8, 8) == BG, "PRIO: un tile opaco del color de BG_COLOR también tapa");
  escribir(0x00, 64 + 1, 0x20); pintar();
  check(px(8, 8) == C1, "B.6-6: PRIO=1 sobre fondo vacío deja el sprite 0");

  fila_de(11, 100); ir_a_linea(v, 200);
  check(!ovf(), "B.6-7: 11 sprites no activan OVERFLOW");
  fila_de(12, 100); ir_a_linea(v, 199);
  check(!ovf(), "B.6-8: OVERFLOW 0 antes de la línea");
  v.tick_frame(); check(ovf(), "B.6-8: OVERFLOW 1 con 12 exactos");
  for (int i = 0; i < 16; i++) v.tick_frame(); // 216: primera línea sin sprites
  check(ovf(), "B.6-8: OVERFLOW se arrastra una línea física");
  v.tick_frame(); check(!ovf(), "B.6-8: y luego cae");
  pintar(); check(vistos(0, 11, 12), "B.6-8: los 12 se dibujan");
  ir_a_linea(v, 482);
  check(!ovf(), "B.6-9: en VBLANK no informa de y=100");
  fila_de(12, 232); ir_a_linea(v, 482);
  check(ovf(), "B.6-9: en VBLANK informa de y=239");

  limpiar_oam(v);
  poner_sprite(v, 0, 312 & 0xFF, 50, 1, 0x14, 0); pintar();
  check(px(311, 50) == BG && px(312, 50) == C1 && px(319, 50) == C1 &&
            px(0, 50) == BG, "B.6-10: 2x en X=312 se recorta");
  limpiar_oam(v);
  poner_sprite(v, 0, 511 & 0xFF, 50, 1, 0x04, 0); pintar();
  check(px(0, 50) == BG && px(319, 50) == BG, "B.6-11: X=511 no se ve");

  // B.3: un temporal de plano 0 por banco; el OAM solo usa 8 bits de dirección.
  escribir(0x80, 9 * 8, 0x55);  // plano 0 de fondo...
  escribir(0xC8, 5 * 8, 0xAA);  // ...y de sprite intercalado
  escribir(0xA0, 9 * 8, 0);
  escribir(0xE8, 5 * 8, 0);
  check(v.bg_patterns()[9 * 16] == 0x55, "B.3: temporal de fondo propio");
  check(v.sprite_patterns()[5 * 16] == 0xAA, "B.3: temporal de sprite propio");
  escribir(0xC0, 0x100 + 2, 7);
  check(v.oam()[2] == 7, "B.3: OAM ignora los bits altos de la dirección");
}

// Cortes de banda: sin redondear a filas de tile, y la inferior existe aunque no
// haya superior ($D809 = $FF).
static void test_bandas() {
  Video v;
  bool resp = false;
  limpiar_oam(v);
  for (int fila = 0; fila < 8; fila++) // tile 1 = color 1; columna 1 del mapa
    for (int plano = 0; plano < 2; plano++) {
      v.write(0xD800, (uint8_t)(8 + fila), resp);
      v.write(0xD801, plano ? 0xA0 : 0x80, resp);
      v.write(0xD802, plano ? 0 : 0xFF, resp);
    }
  for (int f = 0; f < 32; f++) {
    v.write(0xD800, (uint8_t)(f * 64 + 1), resp);
    v.write(0xD801, (uint8_t)((f * 64 + 1) >> 8), resp);
    v.write(0xD802, 1, resp);
  }
  v.write(0xD80E, 100, resp); // solo banda inferior, en 100 (no múltiplo de 8)
  v.write(0xD80F, 8, resp);   // con scroll 8: la columna 1 pasa a x = 0
  std::vector<uint32_t> f((size_t)Video::W * Video::H, 0);
  v.render(f.data());
  check(f[99 * Video::W] == 0xFF4488CC, "bandas: la 99 sigue en la media");
  check(f[100 * Video::W] == 0xFF0000AA, "bandas: la 100 ya es la inferior");
  check(v.scroll().colision_x == 8, "bandas: la colisión usa la inferior sin superior");
}

// $D818 elige la entrada de paleta del trazo de la fuente. Se aplica al
// re-expandir (SETUP $D816), no al escribir el registro.
static void test_font_color() {
  Video v;
  bool resp = false;
  // Se usa "!" ($21): $20 es el espacio y no tiene tinta, así que no probaría nada.
  const int base = 0x21 * 16;  // "!": glifo con tinta ($20 es el espacio)

  // Por defecto el color es 3 (blanco): ambos planos llevan el glifo.
  check(v.font_color() == 3, "font_color: arranca en 3 (blanco)");
  const uint8_t p0_blanco = v.bg_patterns()[base];
  check(p0_blanco != 0, "font_color: con color 3 el glifo tiene tinta");

  // Color 0 -> invisible: los dos planos a cero tras el setup.
  v.write(0xD818, 0x00, resp);
  check(v.font_color() == 0, "font_color: D818 guarda el valor");
  check(v.bg_patterns()[base] == p0_blanco,
        "font_color: escribir D818 no repinta (sigue igual)");
  v.write(0xD816, 0x01, resp); // SETUP: re-expande la fuente
  for (int i = 0; i < 16; i++) v.tick_frame(); // termina el BUSY
  check(v.bg_patterns()[base] == 0x00 && v.bg_patterns()[base + 1] == 0x00,
        "font_color: color 0 deja la fuente invisible");

  // Color 1 -> solo plano 0.
  v.write(0xD818, 0x01, resp);
  v.write(0xD816, 0x01, resp);
  for (int i = 0; i < 16; i++) v.tick_frame();
  check(v.bg_patterns()[base] != 0x00 && v.bg_patterns()[base + 1] == 0x00,
        "font_color: color 1 usa solo el plano 0");

  // Color 2 -> solo plano 1.
  v.write(0xD818, 0x02, resp);
  v.write(0xD816, 0x01, resp);
  for (int i = 0; i < 16; i++) v.tick_frame();
  check(v.bg_patterns()[base] == 0x00 && v.bg_patterns()[base + 1] != 0x00,
        "font_color: color 2 usa solo el plano 1");

  // Color 3 -> los dos planos (vuelve al blanco).
  v.write(0xD818, 0x03, resp);
  v.write(0xD816, 0x01, resp);
  for (int i = 0; i < 16; i++) v.tick_frame();
  check(v.bg_patterns()[base] != 0x00 && v.bg_patterns()[base + 1] != 0x00,
        "font_color: color 3 vuelve a los dos planos");

  // Solo se usan los 2 bits bajos.
  v.write(0xD818, 0xFF, resp);
  check(v.font_color() == 3, "font_color: solo cuenta los 2 bits bajos");
}

int main() {
  std::printf("test_video: Core de Video\n");
  test_paleta_satura();
  test_patrones_demo();
  test_arranque_muestra_cara();
  test_solid_hit();
  test_solid_hit_scroll();
  test_solid_hit_banda();
  test_scroll_vertical_ancho();
  test_barrido();
  test_sprites_line_buffer();
  test_casos_manual();
  test_bandas();
  test_font_color();
  if (fallos) {
    std::printf("test_video: %d FALLOS\n", fallos);
    return 1;
  }
  std::printf("test_video: OK\n");
  return 0;
}
