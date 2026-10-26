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

  // El setup deja 0..2 a cero y limpia tilemap/atributos.
  v.write(0xD816, 1, resp);
  check(v.read(0xD817, resp) == 0x01, "setup: BUSY activo tras $D816");
  bool ceros = true;
  for (int patron = 0; patron <= 2; patron++)
    for (int i = 0; i < 16; i++)
      if (sp[patron * 16 + i] != 0) ceros = false;
  check(ceros, "setup: patrones de sprite 0..2 a cero");

  bool tm_cero = true;
  for (uint8_t t : v.tilemap()) if (t != 0) tm_cero = false;
  check(tm_cero, "setup: tilemap a cero");

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

int main() {
  std::printf("test_video: Core de Video\n");
  test_paleta_satura();
  test_patrones_demo();
  test_arranque_muestra_cara();
  test_barrido();
  if (fallos) {
    std::printf("test_video: %d FALLOS\n", fallos);
    return 1;
  }
  std::printf("test_video: OK\n");
  return 0;
}
