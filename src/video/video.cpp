#include "video/video.h"

#include <cstdio>
#include <cstring>

#include "video/font_c64.h"

// Carga la fuente C64 en los patrones 1bpp -> 2bpp (tiles $20..$7F).
// En el FPGA lo hace la fase de setup del video_core; aquí se hace al construir
// la clase, porque el software espera encontrarla ya puesta.
//
// El color del trazo lo elige $D818 (`font_color_`: 0 = invisible, 1, 2 y 3 por
// defecto). El RTL lo resuelve con MASK_LO/MASK_HI, así que un solo bit de la
// fuente alimenta uno u otro plano:
//   plano0 = byte AND MASK_LO[color]      plano1 = byte AND MASK_HI[color]
//     color 0 -> 00/00 (invisible)   color 1 -> FF/00
//     color 2 -> 00/FF               color 3 -> FF/FF
void Video::load_default_font() {
  static const uint8_t kMaskLo[4] = {0x00, 0xFF, 0x00, 0xFF};
  static const uint8_t kMaskHi[4] = {0x00, 0x00, 0xFF, 0xFF};
  const uint8_t mask_lo = kMaskLo[font_color_ & 0x03];
  const uint8_t mask_hi = kMaskHi[font_color_ & 0x03];

  for (int ch = 0; ch < 96; ch++) {
    const int tile = 0x20 + ch;
    for (int row = 0; row < 8; row++) {
      const uint8_t bits = kFontC64[ch * 8 + row];
      fgpat_[tile * 16 + row * 2] = bits & mask_lo;      // plano 0
      fgpat_[tile * 16 + row * 2 + 1] = bits & mask_hi;  // plano 1
    }
  }
}

// Patrones de FONDO 0..7 que graba la secuencia de inicializacion (fase 2 de
// video_core.vhd). Son 8 tiles graficos de demo: tierra, cesped, copa de arbol,
// tronco, nube, dos piedras y un matojo. Copiados del RTL tal cual.
//
// Tras un setup ($D816) la fase 2 escribe CEROS en esos mismos tiles, asi que el
// arte de demo solo existe hasta el primer setup.
namespace {
const uint8_t kDemoTilesP0[8][8] = {
    {0xFF, 0xFF, 0xFF, 0xF7, 0xFF, 0xEF, 0xDF, 0xFF},  // 0 tierra
    {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // 1 cesped
    {0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C},  // 2 copa de arbol
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},  // 3 tronco
    {0x00, 0x38, 0x7C, 0xFE, 0xFF, 0xFF, 0xFE, 0x7C},  // 4 nube
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // 5 piedra
    {0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x00},  // 6 piedra 2
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // 7 matojo
};
const uint8_t kDemoTilesP1[8][8] = {
    {0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00},  // 0 tierra
    {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // 1 cesped
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // 2 copa de arbol
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // 3 tronco
    {0x00, 0x38, 0x7C, 0xFE, 0xFF, 0xFF, 0xFE, 0x7C},  // 4 nube
    {0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x00},  // 5 piedra
    {0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},  // 6 piedra 2
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // 7 matojo
};
}  // namespace

void Video::load_demo_bg_tiles() {
  for (int tile = 0; tile < 8; tile++) {
    for (int row = 0; row < 8; row++) {
      fgpat_[tile * 16 + row * 2] = kDemoTilesP0[tile][row];
      fgpat_[tile * 16 + row * 2 + 1] = kDemoTilesP1[tile][row];
    }
  }
}

// Patrones de SPRITE 0..2 que graba la secuencia de inicializacion del core
// (video_core.vhd, fase 3). Copiados del RTL: plano 0 = p0, plano 1 = p1.
namespace {
const uint8_t kDemoCaraP0[8] = {0x3C, 0x7E, 0xFF, 0xDB, 0xFF, 0x81, 0x7E, 0x3C};
const uint8_t kDemoCaraP1[8] = {0x00, 0x00, 0x00, 0x24, 0x00, 0x7E, 0x00, 0x00};
const uint8_t kDemoRomboP0[8] = {0x18, 0x3C, 0x7E, 0xFF, 0x7E, 0x7E, 0x3C, 0x18};
const uint8_t kDemoRomboP1[8] = {0x00, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00};
const uint8_t kDemoXP0[8] = {0x81, 0xC3, 0x66, 0x3C, 0x3C, 0x66, 0xC3, 0x81};
const uint8_t kDemoXP1[8] = {0x00, 0x00, 0x00, 0xC3, 0xC3, 0x00, 0x00, 0x00};
} // namespace

// Carga un patron de sprite de 8x8 (dos planos de 8 bytes).
void Video::load_sprite_pattern(int patron, const uint8_t* p0, const uint8_t* p1) {
  for (int row = 0; row < 8; row++) {
    sppat_[patron * 16 + row * 2] = p0[row];
    sppat_[patron * 16 + row * 2 + 1] = p1[row];
  }
}

// Estado de la VRAM tras la inicializacion del core: los patrones de sprite
// 0, 1 y 2 tienen las formas de demo (cara, rombo, X).
//
// En hardware esos patrones NO arrancan vacios: el arranque los escribe con las
// formas de demo y, tras un setup ($D816), los deja a cero. No estan reservados
// (ver docs del core, 07-MANUAL-PROGRAMACION.md §6.0): los 64 son utilizables,
// pero un programa que use el 0..2 debe ESCRIBIRLOS en vez de asumir que estan
// vacios, o verá la cara en la placa.
void Video::load_demo_sprite_patterns() {
  load_sprite_pattern(0, kDemoCaraP0, kDemoCaraP1);
  load_sprite_pattern(1, kDemoRomboP0, kDemoRomboP1);
  load_sprite_pattern(2, kDemoXP0, kDemoXP1);
}

// El setup ($D816) deja los patrones 0..2 a cero (fase 3 con setup_latch=1).
void Video::clear_demo_sprite_patterns() {
  for (int patron = 0; patron <= 2; patron++)
    for (int i = 0; i < 16; i++) sppat_[patron * 16 + i] = 0;
}

// Efecto de la secuencia de inicializacion: fases 0..4 del core.
// Se ejecuta al FINAL del setup, no al escribirlo (ver $D816).
void Video::aplicar_setup() {
  if (init_arranque_) { // reset: las mismas fases, con los datos de arranque
    init_arranque_ = false;
    tilemap_.fill(0x20);
    attrib_.fill(0);
    load_demo_bg_tiles();
    load_demo_sprite_patterns();
    load_default_font();
    return;
  }
  // Fase 0: tilemap a cero. Fase 1: atributos a cero.
  tilemap_.fill(0);
  attrib_.fill(0);
  // Fase 2: tiles de fondo 0..7 a cero (con setup_latch=1 no son demo).
  for (int i = 0; i < 8 * 16; i++) fgpat_[i] = 0;
  // Fase 3: patrones de sprite 0..2 a cero.
  clear_demo_sprite_patterns();
  // Fase 4: la fuente SI se re-expande.
  load_default_font();
}

Video::Video() {
  default_palette();
  // Fase 0 del init de ARRANQUE: tilemap a $20 (espacio). El SETUP lo deja a 0.
  tilemap_.fill(0x20);
  // STATUS arranca con VIDEO_READY (bit 4) activo: el setup de hardware ya
  // terminó cuando el CPU empieza a ejecutar.
  setup_busy_ = 0x10;
  // Al arrancar, el FPGA carga la fuente C64 en los patrones ASCII. Sin esto
  // los juegos que escriben texto no muestran nada.
  load_default_font();
  // Los tiles de fondo 0..7 traen arte de demo (fase 2 del init). Se borran con
  // el primer setup, igual que los patrones de sprite 0..2.
  load_demo_bg_tiles();
  // Y las formas de demo en los patrones de sprite 0..2 (fase 3 del init).
  load_demo_sprite_patterns();
}

bool Video::decodes(uint16_t addr) const {
  return addr >= 0xD800 && addr <= 0xD818;
}

uint8_t Video::read(uint16_t addr, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xD803:                         // STATUS
      // Se compone al leer; leer NO limpia nada (manual del core, B.5).
      return componer_status();
    case 0xD817: return setup_busy_;     // SETUP_ST
    default: resp = false; return 0;
  }
}

void Video::write(uint16_t addr, uint8_t data, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xD800: vid_addr_lo_ = data; break;
    case 0xD801: vid_addr_hi_ = data; break;
    case 0xD802: write_vram(vid_addr_lo_, vid_addr_hi_, data); break;
    case 0xD804: scroll_x_lo_ = data; break;
    case 0xD805: scroll_x_hi_ = data; break;
    case 0xD806: scroll_y_lo_ = data; break;
    case 0xD807: scroll_y_hi_ = data; break;
    case 0xD809: raster0_ = data; break;
    case 0xD80A: band2_x_lo_ = data; break;
    case 0xD80B: band2_x_hi_ = data; break;
    case 0xD80C: band2_y_lo_ = data; break;
    case 0xD80D: band2_y_hi_ = data; break;
    case 0xD80E: raster1_ = data; break;
    case 0xD80F: band3_x_lo_ = data; break;
    case 0xD810: band3_x_hi_ = data; break;
    case 0xD811: band3_y_lo_ = data; break;
    case 0xD812: band3_y_hi_ = data; break;
    case 0xD813: pal_ptr_ = data & 0x1F; break;
    case 0xD814: pal_buf_lo_ = data; break;
    case 0xD815: {
      uint16_t rgb = pal_buf_lo_ | ((uint16_t)(data & 0x0F) << 8);
      pal_[pal_ptr_] = rgb;
      // El auto-incremento SATURA en 31, como el hardware (video_bus.vhd:
      // "if pal_ptr_r /= 11111" -> no incrementa). Envolver a 0 hacía que la
      // entrada 31 fuese inalcanzable y que la escritura siguiente pisara
      // pal_bg(0), que es el color de fondo global (BG_COLOR).
      if (pal_ptr_ != 31) pal_ptr_++;
      break;
    }
    case 0xD816: {
      // SETUP por hardware ($D816).
      //
      // En el core esto relanza las fases 0..4 de la inicializacion: limpia
      // tilemap, atributos, fondo 0..7 y sprites 0..2, y re-expande la fuente.
      // Mientras dura (init_done='0') el core IGNORA las escrituras del CPU a
      // la VRAM (video_core.vhd:965) y deja BUSY=1 en $D817.
      //
      // El emulador dura lo mismo (en lineas enteras) y debe reproducir
      // el bloqueo: si se limpiara de golpe, un programa que escriba justo
      // despues del setup veria su escritura sobrevivir aqui y perderse en la
      // placa. Por eso el setup queda PENDIENTE y se ejecuta al finalizar el
      // BUSY (en tick_frame), que es cuando el hardware libera init_done.
      setup_busy_ = 0x01;
      setup_lineas_ = SETUP_LINEAS;
      init_arranque_ = false;
      break;
    }
    case 0xD818:
      // COLOR DE LA FUENTE (video_bus.vhd): solo los 2 bits bajos.
      //   Escribirlo NO repinta: los patrones de la fuente ya estan expandidos
      //   en VRAM. Hay que disparar SETUP ($D816) para que se re-expanda con el
      //   color nuevo (fase 4 del init).
      //   El valor de arranque es 3 (blanco), asi que el software antiguo no
      //   nota el cambio.
      font_color_ = data & 0x03;
      break;
    default: resp = false; break;
  }
}

// Frame del hardware: 858 x 525 píxeles a 27 MHz = 59,94 Hz (16,683 ms).
void Video::tick_frame() {
  // El barrido real tiene 525 líneas y VBLANK es "v_cnt >= 480": 45 líneas de
  // 525, o sea 8,57 % del frame (~1,43 ms). Aquí se emula línea a línea para
  // que el pulso dure lo mismo que en el FPGA (antes duraba 1 de cada 4
  // frames, un 25 %, y el ritmo no coincidía con el hardware).
  if (++linea_ >= H_LINES) linea_ = 0;
  // SOLID_HIT se recalcula al ENTRAR en el blanking.
  if (linea_ == V_VISIBLE) barrido_colision();

  // OVERFLOW = lleno(v) OR lleno(v-1). El evaluador NO se detiene en VBLANK: en
  // las líneas 480-524 repite y = 239.
  lleno_previo_ = lleno_;
  Entrada lb[NSL];
  lleno_ = evaluar_linea(linea_ < V_VISIBLE ? linea_ / 2 : H - 1, lb) >= NSL;

  // Fin del setup: el core libera init_done. El resultado se aplica AQUI y no al
  // escribir $D816 para que se descarten las escrituras hechas durante el BUSY.
  if (setup_lineas_ > 0 && --setup_lineas_ == 0) {
    aplicar_setup();
    setup_busy_ = 0x10;
  }
}

uint8_t Video::componer_status() const {
  return (uint8_t)((linea_ >= V_VISIBLE ? 0x80 : 0x00) |
                   ((lleno_ || lleno_previo_) ? 0x40 : 0x00) |
                   (solid_hit_ ? 0x20 : 0x00) |
                   ((setup_busy_ & 0x01) ? 0x00 : 0x10));
}

// Banda activa (0 superior, 1 media, 2 inferior) en cada línea FÍSICA, con los
// registros de ahora. Como mucho hay una transición por línea física y ocurre al
// TERMINARLA, así que la primera línea física de la línea lógica R usa todavía la
// banda anterior. `fin` recibe la banda tras la última línea visible (la que ve
// el barrido de colisión).
void Video::calcular_bandas(std::array<uint8_t, V_VISIBLE>& bandas, int& fin) const {
  int b = (raster0_ == 0xFF) ? 1 : 0;
  for (int v = 0; v < V_VISIBLE; v++) {
    bandas[v] = (uint8_t)b;
    const int y = v >> 1;
    if (b == 0 && y >= raster0_) b = 1;
    else if (b == 1 && raster1_ != 0xFF && y >= raster1_) b = 2;
  }
  fin = b;
}

// Scroll en vigor, tal como lo ve el barrido de colisión.
//
// El hardware tiene UNA señal scroll_x/scroll_y que se reescribe en cada
// transición de banda; en el blanking vale lo que dejó la última banda dibujada
// (ver calcular_bandas()). Ver barrido_colision().
Video::Scroll Video::scroll() const {
  Scroll s{};
  s.x = scroll_x_lo_ | ((scroll_x_hi_ & 0x07) << 8);
  s.y = scroll_y_lo_ | ((scroll_y_hi_ & 0x07) << 8);
  s.band2_x = band2_x_lo_ | ((band2_x_hi_ & 0x07) << 8);
  s.band2_y = band2_y_lo_ | ((band2_y_hi_ & 0x07) << 8);
  s.band3_x = band3_x_lo_ | ((band3_x_hi_ & 0x07) << 8);
  s.band3_y = band3_y_lo_ | ((band3_y_hi_ & 0x07) << 8);
  s.raster0 = raster0_;
  s.raster1 = raster1_;
  s.colision_x = s.x;
  s.colision_y = s.y;
  std::array<uint8_t, V_VISIBLE> bandas;
  int fin = 1;
  calcular_bandas(bandas, fin);
  if (fin == 2) {
    s.colision_x = s.band3_x;
    s.colision_y = s.band3_y;
  } else if (fin == 0) { // raster0 entre 240 y 254: nunca sale de la superior
    s.colision_x = s.band2_x;
    s.colision_y = s.band2_y;
  }
  return s;
}

// Celda que consulta el barrido para un sprite. Mismo cálculo que
// barrido_colision(), extraído para que no puedan divergir.
int Video::celda_colision(int sprite) const {
  if (sprite < 0 || sprite >= 32) return -1;
  const Scroll s = scroll();

  const int base = sprite * 5;
  const uint8_t flags = oam_[base + 3];
  const uint8_t control = oam_[base + 4];

  const int ox = oam_[base + 0] | ((flags & 0x04) ? 0x100 : 0);
  const int oy = oam_[base + 1];

  int dx = control & 0x07;
  int dy = (control >> 3) & 0x07;
  if (flags & 0x10) { // SCALE2X
    dx = (dx == 7) ? 15 : dx * 2;
    dy = (dy == 7) ? 15 : dy * 2;
  }

  const int px = (ox + dx) & 0x1FF;
  const int py = (oy + dy) & 0xFF;
  const int wx = (px + s.colision_x) & 0x7FF;
  const int wy = (py + s.colision_y) & 0x7FF;
  return ((wy >> 3) & 0x1F) * MAP_W + ((wx >> 3) & 0x3F);
}

// Barrido de colisión sprite <-> tile sólido (video_core.vhd, fase de blanking).
//
// Para cada uno de los 32 sprites:
//   punto = (X + dx, Y + dy), con dx/dy del COLL_POINT (byte +4 del OAM)
//   mundo = punto + scroll
//   celda = (mundo_y / 8 mod 32) * 64 + (mundo_x / 8 mod 64)
// y si el bit 4 del atributo de esa celda está a 1, hay colisión.
//
// Detalles fieles al RTL que se ven en el código de abajo:
//   - El byte COLL_POINT se IGNORA si SCALE2X está activo salvo para escalar:
//     dx,dy se multiplican por 2 (y 7 pasa a 15, no a 14).
//   - NO se descartan los sprites con Y >= 248: el barrido recorre los 32
//     siempre (a diferencia del dibujado, que sí los salta).
//   - SE APLICA SCROLL (cc645d3): el punto de pantalla se pasa a coordenadas de
//     mundo para leer la misma celda que se ve bajo el sprite. Antes se usaba
//     la posición de pantalla como si fuera de mundo, y con scroll activo
//     colisionaba contra otra celda.
//   - El scroll es el de la BANDA ACTIVA (scroll_x/scroll_y del RTL, que cambian
//     al cruzar raster0/raster1). Como el barrido ocurre en el blanking, en la
//     práctica es el de la última banda del frame.
//   - El bit se recalcula entero cada frame: no es sticky
//     (clear_stats está declarado en el RTL pero sin usar).
void Video::barrido_colision() {
  // El scroll que usa el barrido sale de scroll() (misma fuente que
  // celda_colision(), para que el depurador vea exactamente lo mismo): es el
  // de la ÚLTIMA banda dibujada, no el de la superior.
  bool hit = false;
  for (int i = 0; i < 32 && !hit; i++) {
    const int celda = celda_colision(i);
    if (celda >= 0 && (attrib_[celda] & 0x10)) hit = true; // bit 4 = SOLIDO
  }
  solid_hit_ = hit;
}

void Video::reset_placa() {
  vid_addr_lo_ = vid_addr_hi_ = 0;
  scroll_x_lo_ = scroll_x_hi_ = scroll_y_lo_ = scroll_y_hi_ = 0;
  band2_x_lo_ = band2_x_hi_ = band2_y_lo_ = band2_y_hi_ = 0;
  band3_x_lo_ = band3_x_hi_ = band3_y_lo_ = band3_y_hi_ = 0;
  raster0_ = raster1_ = 0xFF;
  solid_hit_ = lleno_ = lleno_previo_ = false;
  linea_ = 0;
  pixeles_ = 0;
  setup_busy_ = 0x01;
  setup_lineas_ = SETUP_LINEAS;
  init_arranque_ = true;
}

void Video::write_vram(uint8_t lo, uint8_t hi, uint8_t data) {
  // Mientras la inicializacion (o un setup) esta en curso, el core IGNORA las
  // escrituras del CPU a la VRAM: el puerto A lo controla la FSM
  // (video_core.vhd:965, "vid_we = '1' and init_done = '1'").
  //
  // Scroll y paleta NO pasan por aqui, asi que siguen funcionando: eso coincide
  // con el hardware, que las deja escribir en cualquier momento.
  if (setup_busy_ & 0x01) return;

  uint8_t area = (hi >> 6) & 0x03;
  uint8_t pat_hi = (hi >> 5) & 0x01;
  uint8_t bit3 = (hi >> 3) & 0x01;
  uint8_t dir_hi = hi & 0x07;
  uint16_t d = lo | ((uint16_t)dir_hi << 8);

  switch (area) {
    case 0: // tilemap 0..2047
      if (d < 2048) tilemap_[d] = data;
      break;
    case 1: // atributos 0..2047
      if (d < 2048) attrib_[d] = data;
      break;
    case 2: { // patrón de fondo (plano 0/1), d = tile*8 + fila
      if (d < 2048) {
        uint16_t tile = d / 8;
        uint16_t fila = d % 8;
        uint16_t idx = tile * 16 + fila * 2 + pat_hi;
        if (idx < fgpat_.size()) {
          if (pat_hi == 0) {
            plano0_fondo_ = data;
          } else {
            // Al escribir plano 1, guarda la palabra completa.
            fgpat_[tile * 16 + fila * 2] = plano0_fondo_;
            fgpat_[tile * 16 + fila * 2 + 1] = data;
          }
        }
      }
      break;
    }
    case 3: {
      if (bit3 == 0) { // OAM (32 sprites × 5 bytes)
        // Solo cuentan los 8 bits bajos ($C0-$C7 equivalen); >= 160 se ignora.
        if (lo < 160) oam_[lo] = data;
      } else { // patrón de sprite (plano 0/1), d = patron*8 + fila
        if (d < 512) {
          uint16_t patron = d / 8;
          uint16_t fila = d % 8;
          uint16_t idx = patron * 16 + fila * 2 + pat_hi;
          if (idx < sppat_.size()) {
            if (pat_hi == 0) {
              plano0_sprite_ = data;
            } else {
              sppat_[patron * 16 + fila * 2] = plano0_sprite_;
              sppat_[patron * 16 + fila * 2 + 1] = data;
            }
          }
        }
      }
      break;
    }
  }
}

void Video::default_palette() {
  // Paletas por defecto (RGB444) del manual §4.1 y §4.2.
  // Fondo:
  //   pal0: (transp), azul $00A, cian $0CF, blanco $FFF
  //   pal1: (transp), marrón $A62, gris $AAA, blanco
  //   pal2: (transp), verde $0A0, verde oscuro $060, verde
  //   pal3: (transp), gris $888, marrón $840, BG_COLOR = $48C
  uint16_t bg[16] = {
    0x000, 0x00A, 0x0CF, 0xFFF, // paleta 0
    0x000, 0xA62, 0xAAA, 0xFFF, // paleta 1
    0x000, 0x0A0, 0x060, 0x0F0, // paleta 2
    0x000, 0x888, 0x840, 0x48C, // paleta 3 (color3 = BG_COLOR)
  };
  for (int i = 0; i < 16; i++) pal_[i] = bg[i];

  // Sprite (16 entradas):
  uint16_t sp[16] = {
    0x000, 0xF80, 0x840, 0x000, // paleta 0
    0x000, 0x00F, 0x0FF, 0xFFF, // paleta 1
    0x000, 0xF0F, 0xF00, 0xFFF, // paleta 2
    0x000, 0x0F0, 0xF80, 0xFFF, // paleta 3
  };
  for (int i = 0; i < 16; i++) pal_[16 + i] = sp[i];
}

uint32_t Video::rgb444_to_rgba(uint16_t rgb444) const {
  // Expande nibble (0-F) a byte (0-255) duplicando el nibble.
  uint8_t r = (uint8_t)((rgb444 >> 8) & 0xF);
  uint8_t g = (uint8_t)((rgb444 >> 4) & 0xF);
  uint8_t b = (uint8_t)(rgb444 & 0xF);
  uint8_t R = (uint8_t)((r << 4) | r);
  uint8_t G = (uint8_t)((g << 4) | g);
  uint8_t B = (uint8_t)((b << 4) | b);
  return 0xFF000000u | (R << 16) | (G << 8) | B;
}

uint16_t Video::pal_index(int area, uint8_t attrib_byte, uint8_t color) const {
  (void)area;
  // Para una celda del fondo, el atributo usa bits 1:0 = paleta.
  uint8_t pal = attrib_byte & 0x03;
  uint8_t idx = (pal << 2) | color;
  return idx & 0x1F;
}

// Evaluador de sprites de una línea lógica: los primeros NSL del OAM que la
// cruzan en Y, en orden de índice. La X no influye: uno fuera de pantalla, o sin
// píxeles opacos, también ocupa hueco. Sin envoltura vertical (y como py <= 239,
// los de Y >= 248 quedan fuera solos).
int Video::evaluar_linea(int py, Entrada* lb) const {
  int n = 0;
  for (int i = 0; i < 32 && n < NSL; i++) {
    const uint8_t* o = &oam_[i * 5];
    const uint8_t flags = o[3];
    const bool s2x = (flags & 0x10) != 0;
    const int dy = py - o[1];
    if (dy < 0 || dy >= (s2x ? 16 : 8)) continue;
    int fila = s2x ? dy >> 1 : dy;
    if (flags & 0x80) fila = 7 - fila; // FLIP_Y
    const int w = (o[2] & 0x3F) * 16 + fila * 2;
    lb[n++] = Entrada{o[0] | ((flags & 0x04) << 6), s2x, (flags & 0x40) != 0,
                      (flags & 0x20) != 0, (uint8_t)(flags & 0x03), sppat_[w],
                      sppat_[w + 1]};
  }
  return n;
}

void Video::render(uint32_t* out) {
  // Se pinta leyendo la VRAM VIVA, igual que el video_core.vhd, que direcciona
  // la celda de cada pixel mientras recorre la línea de barrido.
  //
  // Antes se copiaba todo el estado a una instantánea (_snapshot_) al entrar
  // aquí. Eso evitaba frames rasgados, pero rompía las escrituras DOBLES del
  // 6502: en un RMW el chip escribe primero el valor VIEJO y luego el nuevo.
  // Con la copia, la escritura vieja ganaba el frame entero y el efecto se
  // veía invertido (p. ej. Pac-Man: la power pellet nunca desaparecía).
  //
  // En el hardware real no hay carrera posible: el juego espera el VBLANK
  // antes de tocar la VRAM (vc_wait_vblank en la lib del juego).
  const Scroll s = scroll();
  const int scx[3] = {s.band2_x, s.x, s.band3_x};
  const int scy[3] = {s.band2_y, s.y, s.band3_y};
  std::array<uint8_t, V_VISIBLE> bandas;
  int fin = 1;
  calcular_bandas(bandas, fin);

  // BG_COLOR = paleta de fondo entrada 15.
  const uint32_t bg_color = rgb444_to_rgba(pal_[15]);

  // Modelo de referencia del manual del core, B.4.
  for (int py = 0; py < H; py++) {
    // La imagen es de 240 líneas y el corte de banda cae entre las dos líneas
    // físicas de la línea lógica R; se toma la segunda, ya con la banda nueva.
    const int banda = bandas[py * 2 + 1];
    const int wy = (py + scy[banda]) & 0x7FF;

    Entrada lb[NSL];
    const int n = evaluar_linea(py, lb);

    for (int px = 0; px < W; px++) {
      // --- Fondo ---
      const int wx = (px + scx[banda]) & 0x7FF;
      const int cell = ((wy >> 3) & (MAP_H - 1)) * MAP_W + ((wx >> 3) & (MAP_W - 1));
      const int word = tilemap_[cell] * 16 + (wy & 7) * 2;
      const int bit = 7 - (wx & 7);
      const uint8_t bc = (uint8_t)((((fgpat_[word + 1] >> bit) & 1) << 1) |
                                   ((fgpat_[word] >> bit) & 1));

      // --- Sprite: el OPACO de menor índice que cubre el píxel ---
      const Entrada* pick = nullptr;
      uint8_t sc = 0;
      for (int k = 0; k < n; k++) {
        int d = px - lb[k].x; // sin envoltura horizontal
        if (d < 0 || d >= (lb[k].scale2x ? 16 : 8)) continue;
        if (lb[k].scale2x) d >>= 1;
        if (lb[k].flip_x) d = 7 - d;
        sc = (uint8_t)((((lb[k].p1 >> (7 - d)) & 1) << 1) |
                       ((lb[k].p0 >> (7 - d)) & 1));
        if (sc) {
          pick = &lb[k];
          break;
        }
      }

      // PRIO es el del sprite ELEGIDO: uno opaco con PRIO=1 oculta a los de
      // índice mayor aunque a él lo tape el fondo.
      uint32_t pixel = bg_color;
      if (pick && (!pick->prio || bc == 0))
        pixel = rgb444_to_rgba(pal_[16 + pick->pal * 4 + sc]);
      else if (bc)
        pixel = rgb444_to_rgba(pal_[pal_index(0, attrib_[cell], bc)]);
      out[py * W + px] = pixel;
    }
  }

  // Guarda lo que se acaba de pintar: es el frame que salio por pantalla, y es
  // lo que sirve /screen.png. 320x240x4 = 300 KB, una copia que el compilador
  // vectoriza (<0,1 ms).
  const size_t n = (size_t)W * H;
  if (last_frame_.size() != n) last_frame_.resize(n);
  std::memcpy(last_frame_.data(), out, n * sizeof(uint32_t));
}
