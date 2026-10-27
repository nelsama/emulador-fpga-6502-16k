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

  // Escribir algo en la VRAM ANTES del setup, para ver que se borra.
  v.write(0xD800, 0x05, resp);  // dir lo
  v.write(0xD801, 0x00, resp);  // area 00 = tilemap
  v.write(0xD802, 0x42, resp);
  check(v.tilemap()[5] == 0x42, "setup: escritura previa aplicada");

  // $D816 solo dispara: BUSY=1 y la escritura siguiente del CPU se IGNORA,
  // como en el hardware (init_done='0').
  v.write(0xD816, 1, resp);
  check(v.read(0xD817, resp) == 0x01, "setup: BUSY activo tras $D816");
  v.write(0xD800, 0x07, resp);
  v.write(0xD801, 0x00, resp);
  v.write(0xD802, 0x99, resp);
  check(v.tilemap()[7] == 0x00, "setup: escritura durante BUSY se ignora");

  // Al terminar el BUSY (tick_frame) se aplica: todo a cero.
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

int main() {
  std::printf("test_video: Core de Video\n");
  test_paleta_satura();
  test_patrones_demo();
  test_arranque_muestra_cara();
  test_solid_hit();
  test_solid_hit_scroll();
  test_solid_hit_banda();
  test_barrido();
  if (fallos) {
    std::printf("test_video: %d FALLOS\n", fallos);
    return 1;
  }
  std::printf("test_video: OK\n");
  return 0;
}
