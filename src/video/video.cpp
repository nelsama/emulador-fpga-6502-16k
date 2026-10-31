#include "video/video.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "video/font_c64.h"

// Carga la fuente C64 en los patrones 1bpp -> 2bpp (tiles $20..$7F).
// En el FPGA lo hace la fase de setup del video_core; aquí se hace al construir
// la clase, porque el software espera encontrarla ya puesta.
void Video::load_default_font() {
  for (int ch = 0; ch < 96; ch++) {
    const int tile = 0x20 + ch;
    for (int row = 0; row < 8; row++) {
      const uint8_t bits = kFontC64[ch * 8 + row];
      // 1bpp -> 2bpp: un pixel encendido usa el color 3 (blanco), el resto 0.
      fgpat_[tile * 16 + row * 2] = bits;     // plano 0
      fgpat_[tile * 16 + row * 2 + 1] = bits; // plano 1 (color 3)
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
  // STATUS arranca con VIDEO_READY (bit 4) activo: el setup de hardware ya
  // terminó cuando el CPU empieza a ejecutar.
  status_ = 0x10;
  setup_busy_ = 0x10;
  // Al arrancar, el FPGA carga la fuente C64 en los patrones ASCII. Sin esto
  // los juegos que escriben texto no muestran nada.
  load_default_font();
  // Y las formas de demo en los patrones de sprite 0..2 (fase 3 del init).
  load_demo_sprite_patterns();
}

bool Video::decodes(uint16_t addr) const {
  return addr >= 0xD800 && addr <= 0xD817;
}

uint8_t Video::read(uint16_t addr, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xD803:                         // STATUS
      // OVERFLOW se compone al leer: lo pone el barrido de render(), que es
      // donde se llena el line buffer de verdad. status_ solo se refresca en
      // tick_frame(), así que esperar a que pase ese tick daría un frame de
      // retraso respecto al hardware.
      status_ = (status_ & ~0x40) | (sprite_overflow_ ? 0x40 : 0x00);
      return status_;
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
      // El emulador no puede tardar los ~275 us reales, pero SI debe reproducir
      // el bloqueo: si se limpiara de golpe, un programa que escriba justo
      // despues del setup veria su escritura sobrevivir aqui y perderse en la
      // placa. Por eso el setup queda PENDIENTE y se ejecuta al finalizar el
      // BUSY (en tick_frame), que es cuando el hardware libera init_done.
      setup_busy_ = 0x01;
      setup_pendiente_ = true;
      break;
    }
    default: resp = false; break;
  }
}

// Frame del hardware: 858 x 525 píxeles a 27 MHz = 59,94 Hz (16,683 ms).
void Video::tick_frame() {
  // El barrido real tiene 525 líneas y VBLANK es "v_cnt >= 480": 45 líneas de
  // 525, o sea 8,57 % del frame (~1,43 ms). Aquí se emula línea a línea para
  // que el pulso dure lo mismo que en el FPGA (antes duraba 1 de cada 4
  // frames, un 25 %, y el ritmo no coincidía con el hardware).
  static int line = V_VISIBLE; // arranca en vblank: el primer wait lo ve
  if (++line >= H_LINES) line = 0;

  const bool vblank = (line >= V_VISIBLE);
  // Bit 5 (SOLID_HIT) se calcula al ENTRAR en el blanking, que es cuando el
  // hardware hace el barrido (video_core.vhd: el proceso espera
  // "v_cnt = V_VISIBLE and h_cnt = 0").
  //
  // OJO con el estado inicial: el contador arranca EN blanking (line =
  // V_VISIBLE), asi que en la primera llamada vblank_previo_ debe valer true o
  // el primer frame se perderia sin barrer.
  if (vblank && !vblank_previo_) barrido_colision();
  vblank_previo_ = vblank;

  status_ = (vblank ? 0x80 : 0x00) | (sprite_overflow_ ? 0x40 : 0x00) |
            (solid_hit_ ? 0x20 : 0x00) | 0x10;
  if (setup_busy_ & 0x01) {
    // Fin del setup: ahora el core libera init_done y aplica el resultado.
    // Se hace AQUI y no al escribir $D816 para que las escrituras del CPU
    // hechas durante el BUSY se descarten, como en el hardware.
    if (setup_pendiente_) {
      aplicar_setup();
      setup_pendiente_ = false;
    }
    setup_busy_ = 0x10; // VIDEO_READY tras el setup
  }
}

// Scroll en vigor, tal como lo ve el barrido de colisión.
//
// El hardware tiene UNA señal scroll_x/scroll_y que se reescribe en cada
// transición de banda; en el blanking vale lo que dejó la última banda dibujada
// (la INFERIOR si hay split, el normal si no). Ver barrido_colision().
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
  if (raster0_ != 0xFF && raster1_ != 0xFF) {
    s.colision_x = s.band3_x;
    s.colision_y = s.band3_y;
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

void Video::sync_scanlines(long long linea) {
  // `linea` es absoluta (cuenta desde el arranque). Se avanza solo lo que
  // falte, y si el hilo se ha retrasado mucho no se simulan millones de líneas
  // de golpe: se salta al punto correcto (el VBLANK dura 45 líneas; tras un
  // retraso largo lo importante es volver a cuadrar, no reproducir el hueco).
  if (linea <= scanline_) return;
  const long long pendientes = linea - scanline_;
  if (pendientes > H_LINES * 2) {
    scanline_ = linea; // demasiada diferencia: resincronizar de golpe
    tick_frame();
    return;
  }
  while (scanline_ < linea) {
    ++scanline_;
    tick_frame();
#ifdef EMU_VIDEO_TRACE
    // Diagnóstico: registra el INICIO de cada VBLANK (solo flancos), con el
    // tiempo de pared desde el arranque. Compilar con -DEMU_VIDEO_TRACE.
    const bool ahora_vblank = (status_ & 0x80) != 0;
    if (ahora_vblank && !trace_vblank_) {
      using clock = std::chrono::steady_clock;
      static const auto t0 = clock::now();
      const double ms =
          std::chrono::duration<double, std::milli>(clock::now() - t0).count();
      std::fprintf(stderr, "FRAME %.3f ms\n", ms);
    }
    trace_vblank_ = ahora_vblank;
#endif
  }
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
            pending_plane0_ = data;
            plane0_valid_ = true;
          } else {
            // Al escribir plano 1, guarda la palabra completa.
            fgpat_[tile * 16 + fila * 2] = pending_plane0_;
            fgpat_[tile * 16 + fila * 2 + 1] = data;
            plane0_valid_ = false;
          }
        }
      }
      break;
    }
    case 3: {
      if (bit3 == 0) { // OAM (32 sprites × 5 bytes)
        if (d < 160) oam_[d] = data;
      } else { // patrón de sprite (plano 0/1), d = patron*8 + fila
        if (d < 512) {
          uint16_t patron = d / 8;
          uint16_t fila = d % 8;
          uint16_t idx = patron * 16 + fila * 2 + pat_hi;
          if (idx < sppat_.size()) {
            if (pat_hi == 0) {
              pending_plane0_ = data;
              plane0_valid_ = true;
            } else {
              sppat_[patron * 16 + fila * 2] = pending_plane0_;
              sppat_[patron * 16 + fila * 2 + 1] = data;
              plane0_valid_ = false;
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
  const int scx = scroll_x_lo_ | ((scroll_x_hi_ & 0x07) << 8);
  const int scy = scroll_y_lo_ | ((scroll_y_hi_ & 0x07) << 8);

  // BG_COLOR = paleta de fondo entrada 15.
  const uint32_t bg_color = rgb444_to_rgba(pal_[15]);

  // OVERFLOW del frame anterior: en el RTL el pestillo se rearma al principio de
  // cada línea (`h_cnt = 0`); aquí el equivalente por frame es limpiarlo antes
  // de barrer y que lo suban las líneas que desborden.
  sprite_overflow_ = false;

  for (int py = 0; py < H; py++) {
    // Determinar banda y scroll según línea.
    int band_scroll_x = scx;
    int band_scroll_y = scy;
    if (raster0_ != 0xFF && py < (raster0_ & 0xF8)) {
      band_scroll_x = band2_x_lo_ | ((band2_x_hi_ & 0x07) << 8);
      band_scroll_y = band2_y_lo_ | ((band2_y_hi_ & 0x07) << 8);
    } else if (raster1_ != 0xFF && py >= (raster1_ & 0xF8)) {
      band_scroll_x = band3_x_lo_ | ((band3_x_hi_ & 0x07) << 8);
      band_scroll_y = band3_y_lo_ | ((band3_y_hi_ & 0x07) << 8);
    }

    // Posición de mundo = pantalla + scroll, en 11 bits como el RTL
    // (video_core.vhd: y0_world <= resize(y0_log,11) + scroll_y).
    //
    // El módulo a 11 bits y el de la celda (32 filas x 8 px = 256) son
    // equivalentes: envolver antes a 256 o después da lo mismo, porque 256 es
    // múltiplo de 8 y de 32. Se usa 11 bits para que esta ruta y la del barrido
    // de colisión manejen el mismo ancho, en vez de diferir sin motivo.
    int map_y = (py + band_scroll_y) & 0x7FF;
    int tile_row = (map_y / TILE_H) & (MAP_H - 1);
    int pix_y = map_y % TILE_H;

    for (int px = 0; px < W; px++) {
      // 9 bits = 64 columnas x 8 px, igual que x0_world del RTL.
      int map_x = (px + band_scroll_x) & 0x1FF;
      int tile_col = (map_x / TILE_W) & (MAP_W - 1);
      int pix_x = map_x % TILE_W;

      int cell = tile_row * MAP_W + tile_col;
      uint8_t tile = tilemap_[cell];
      uint8_t attr = attrib_[cell];

      // Color del píxel en el patrón de fondo (2bpp planar).
      int word = tile * 16 + pix_y * 2;
      uint8_t p0 = fgpat_[word];
      uint8_t p1 = fgpat_[word + 1];
      int bit = 7 - pix_x;
      int c0 = (p0 >> bit) & 1;
      int c1 = (p1 >> bit) & 1;
      uint8_t color = (uint8_t)((c1 << 1) | c0);

      uint32_t pixel;
      if (color == 0) {
        pixel = bg_color; // transparente -> BG_COLOR
      } else {
        uint16_t pal = pal_index(0, attr, color);
        pixel = rgb444_to_rgba(pal_[pal]);
      }

      out[py * W + px] = pixel;
    }
  }

  // ---- Sprites ----
  //
  // El hardware NO dibuja los 32 sprites uno sobre otro. video_core.vhd usa un
  // LINE BUFFER (NSL = 8 entradas): en el blanking se recolectan los primeros 8
  // sprites del OAM que cruzan la línea, y en el barrido visible se elige para
  // cada píxel el PRIMER sprite de ese buffer que lo cubre (el de menor índice).
  //
  // De ahí salen dos reglas que hay que reproducir:
  //   1. Entre sprites NO hay prioridad por orden de dibujo: gana el de índice
  //      menor. Un sprite de índice mayor se recorta contra el hueco del
  //      anterior y por ahí se ve el FONDO, nunca otro sprite.
  //   2. En una línea con más de 8 sprites que la crucen, los sprites 9º y
  //      siguientes NO SE DIBUJAN (el buffer está lleno); solo activan
  //      OVERFLOW en $D803.
  //
  // Antes se pintaban los 32 en orden, así que el último tapaba al primero y
  // los sprites se veían unos a través de otros, cosa que el chip no hace.
  {
    // Candidatos de una línea del line buffer, en orden de índice (el RTL los
    // guarda recorriendo el OAM de 0 a 31, así que el orden del buffer es el de
    // índice creciente).
    struct Entrada {
      int x;        // X en pantalla (9 bits)
      int y;        // Y en pantalla (byte del OAM, ya filtrado >= 248)
      int width;    // 8 u 16 px segun SCALE2X
      bool prio;
      int tile;
      bool flip_x, flip_y;
      bool scale2x;
      uint8_t spal;
    };

    for (int py = 0; py < H; py++) {
      Entrada lb[8];
      int n = 0;
      bool overflow = false;

      // Fase 1 (equivalente al blanking): recolectar los sprites que cruzan
      // esta línea, en orden de índice, hasta llenar el buffer.
      for (int i = 0; i < 32; i++) {
        const uint8_t* o = &oam_[i * 5];
        const uint8_t y8 = o[1];
        const uint8_t flags = o[3];
        const bool s2x = (flags & 0x10) != 0;
        const int span = s2x ? 16 : 8;
        // Y >= 248 desactiva el DIBUJADO (el barrido de colisión sí los ve).
        if (y8 >= 248) continue;
        if (!(y8 <= py && y8 + span > py)) continue;
        if (n >= 8) {
          overflow = true;  // el buffer está lleno: no entra y no se dibuja
          continue;
        }
        Entrada& e = lb[n++];
        e.x = o[0] | ((flags & 0x04) ? 0x100 : 0);
        e.y = y8;
        e.width = span;
        e.tile = o[2] & 0x3F;
        e.prio = (flags & 0x20) != 0;
        e.flip_x = (flags & 0x40) != 0;
        e.flip_y = (flags & 0x80) != 0;
        e.scale2x = s2x;
        e.spal = flags & 0x03;
      }
      // Igual que `overflow_f` en el RTL: el pestillo se rearma al principio de
      // cada línea y se mantiene si esa línea desborda. Aquí solo se sube (nunca
      // se baja dentro de un mismo frame); el reset por línea lo da el RTL con
      // `h_cnt = 0`, y en el modelo por frame eso equivale a limpiarlo al final.
      if (overflow) sprite_overflow_ = true;

      // Fase 2 (barrido visible): para cada píxel, el PRIMER sprite del buffer
      // que lo cubre decide, igual que el "found" del RTL. Los siguientes NO
      // se pintan encima: si el elegido es transparente en ese píxel, se ve el
      // fondo, no otro sprite.
      for (int px = 0; px < W; px++) {
        const Entrada* pick = nullptr;
        for (int k = 0; k < n; k++) {
          if (lb[k].x <= px && lb[k].x + lb[k].width > px) {
            pick = &lb[k];
            break;
          }
        }
        if (!pick) continue;

        const int ox = px - pick->x;
        // Fila del patrón: el RTL la calcula al recolectar, dividiendo por 2 en
        // 2x (y aplicando FLIP_Y después de escalar).
        int fy = pick->scale2x ? ((py - pick->y) / 2) : (py - pick->y);
        int fx = pick->scale2x ? (ox / 2) : ox;
        if (pick->flip_x) fx = 7 - fx;
        if (pick->flip_y) fy = 7 - fy;

        const int word = pick->tile * 16 + fy * 2;
        const uint8_t p0 = sppat_[word];
        const uint8_t p1 = sppat_[word + 1];
        const int bit = 7 - fx;
        const int c0 = (p0 >> bit) & 1;
        const int c1 = (p1 >> bit) & 1;
        const uint8_t color = (uint8_t)((c1 << 1) | c0);

        if (color == 0) continue;  // transparente: se queda el fondo

        const uint16_t pal = ((uint16_t)pick->spal << 2) | color;
        const uint32_t pixel = rgb444_to_rgba(pal_[16 + pal]);

        // PRIO = 1 -> detrás del fondo: solo se pinta donde el fondo es
        // transparente (el RTL compara con pixcode /= "00", y ahí el frame ya
        // tiene BG_COLOR).
        if (pick->prio) {
          uint32_t& bgpx = out[py * W + px];
          if (bgpx != bg_color) continue;
        }
        out[py * W + px] = pixel;
      }
    }
  }

  // Guarda lo que se acaba de pintar: es el frame que salio por pantalla, y es
  // lo que sirve /screen.png. 320x240x4 = 300 KB, una copia que el compilador
  // vectoriza (<0,1 ms).
  const size_t n = (size_t)W * H;
  if (last_frame_.size() != n) last_frame_.resize(n);
  std::memcpy(last_frame_.data(), out, n * sizeof(uint32_t));
}
