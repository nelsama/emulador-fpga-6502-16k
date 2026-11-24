#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "device.h"

// Motor de vídeo del fpga-6502-16k (tiles + sprites, sin framebuffer).
//
// Replica el coprocesador gráfico estilo NES/VIC-II descrito en
// docs/07-MANUAL-PROGRAMACION.md:
//   - Puerto indirecto de VRAM: $D800 (dir lo), $D801 (área+dir hi), $D802 (dato).
//   - Áreas: tilemap, atributos, patrones de fondo, OAM, patrones de sprite.
//   - Scroll global + split de raster en 3 bandas.
//   - OAM de 32 sprites × 5 bytes.
//   - Paletas RGB444 (32 entradas), color 0 transparente, BG_COLOR = entrada 15.
//   - STATUS con VBLANK / OVERFLOW / SOLID_HIT / VIDEO_READY.
//
// El renderizado real (salida a SDL2) se hace en render.cpp a partir de este
// estado.
class Video : public Device {
public:
  static constexpr int W = 320; // píxeles lógicos de ancho
  static constexpr int H = 240; // píxeles lógicos de alto
  static constexpr int TILE_W = 8;
  static constexpr int TILE_H = 8;
  static constexpr int MAP_W = 64; // celdas de ancho del mapa
  static constexpr int MAP_H = 32; // celdas de alto del mapa

  // Barrido del hardware (video_core.vhd, congelado): 858 x 525 a 27 MHz.
  // VBLANK = v_cnt >= 480, o sea 45 líneas de las 525.
  static constexpr int H_LINES = 525;
  static constexpr int V_VISIBLE = 480;
  static constexpr double FRAME_HZ = 27.0e6 / (858.0 * 525.0); // 59,94 Hz

  static constexpr int NSL = 12; // sprites por línea (NSL de video_core.vhd)
  // Init/SETUP: 4956 píxeles = 5,78 líneas; se redondea a líneas enteras.
  static constexpr int SETUP_LINEAS = 6;

  Video();

  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Se llama una vez por línea de barrido: avanza VBLANK en $D803 con la
  // duración exacta del hardware.
  void tick_frame();

  // Avanza el barrido los ciclos de CPU dados: 8 píxeles por ciclo, 858 por línea.
  void tick_cpu(uint32_t cpu_cycles) {
    pixeles_ += cpu_cycles * 8;
    while (pixeles_ >= 858) {
      pixeles_ -= 858;
      tick_frame();
    }
  }

  // Botón de reset de la placa: relanza el init de arranque (tabla del manual B.3).
  void reset_placa();

  // Genera la imagen (RGBA8888 / 320x240) en el buffer del llamador.
  //
  // Se lee la VRAM VIVA, como el video_core.vhd (que direcciona la celda en
  // cada línea de barrido). Ver el comentario de render() en video.cpp para el
  // razonamiento: congelar la VRAM rompía las escrituras dobles del 6502.
  void render(uint32_t* out);

  // Último frame RENDERIZADO (no la VRAM de ahora): es lo que salió por
  // pantalla. Lo usa el depurador para que /screen.png muestre lo que de
  // verdad se vio y no una reconstrucción a partir de un estado posterior.
  const std::vector<uint32_t>& last_frame() const { return last_frame_; }

  // Tilemap, atributos y OAM (para el renderer y el debugger).
  const std::array<uint8_t, 2048>& tilemap()   const { return tilemap_; }
  const std::array<uint8_t, 2048>& attrib()    const { return attrib_; }
  const std::array<uint8_t, 160>&   oam()      const { return oam_; }
  // Solo para verificar el estado inicial frente al RTL
  // (pruebas y depuracion).
  const std::array<uint8_t, 256 * 8 * 2>& bg_patterns()    const { return fgpat_; }
  const std::array<uint8_t, 64 * 8 * 2>&  sprite_patterns() const { return sppat_; }
  const std::array<uint16_t, 32>&         palette()        const { return pal_; }
  uint8_t palette_ptr() const { return pal_ptr_; }
  // Color del trazo de la fuente ($D818): 0 = invisible, 1, 2, 3 = blanco.
  uint8_t font_color() const { return font_color_; }

  // Registros de scroll y bandas ($D804-$D812). Los necesita el depurador para
  // averiguar qué celda calcula el barrido de colisión: sin ellos no se puede
  // comprobar SOLID_HIT, porque la celda depende del scroll.
  struct Scroll {
    int x, y;           // scroll normal ($D804-$D807)
    int band2_x, band2_y; // banda superior ($D80A-$D80D)
    int band3_x, band3_y; // banda inferior ($D80F-$D812)
    uint8_t raster0;    // $D809 ($FF = sin banda superior)
    uint8_t raster1;    // $D80E ($FF = sin banda inferior)
    // Scroll que ve el barrido de colisión: el de la última banda dibujada.
    int colision_x, colision_y;
  };
  Scroll scroll() const;

  // Celda (índice en el tilemap) que revisa el barrido de colisión para el
  // sprite `n`. Devuelve -1 si el sprite no existe. Con SOLID_HIT y split de
  // raster, esta celda es la que el hardware realmente consulta.
  int celda_colision(int sprite) const;

private:
  uint32_t pixeles_ = 0; // píxeles físicos de la línea en curso
  // Registros directos ($D800-$D817)
  uint8_t vid_addr_lo_ = 0; // $D800
  uint8_t vid_addr_hi_ = 0; // $D801
  // ($D802 es write-only, dispara la escritura)
  uint8_t scroll_x_lo_ = 0, scroll_x_hi_ = 0; // $D804/$D805
  uint8_t scroll_y_lo_ = 0, scroll_y_hi_ = 0; // $D806/$D807
  uint8_t raster0_ = 0xFF;   // $D809
  uint8_t band2_x_lo_ = 0, band2_x_hi_ = 0;   // $D80A/$D80B
  uint8_t band2_y_lo_ = 0, band2_y_hi_ = 0;   // $D80C/$D80D
  uint8_t raster1_ = 0xFF;   // $D80E
  uint8_t band3_x_lo_ = 0, band3_x_hi_ = 0;   // $D80F/$D810
  uint8_t band3_y_lo_ = 0, band3_y_hi_ = 0;   // $D811/$D812
  uint8_t pal_ptr_ = 0;      // $D813
  uint8_t pal_buf_lo_ = 0;   // $D814 (buffer)
  uint8_t setup_busy_ = 0;   // $D817 bit0 (estado setup)
  // COLOR DE LA FUENTE ($D818): entrada de paleta del trazo al expandir la
  // fuente (0 = invisible, 1, 2, 3 = blanco por defecto). Es estable, como el
  // scroll, y se aplica al re-expandir (SETUP), no al escribirlo.
  uint8_t font_color_ = 3;
  // Líneas que le quedan al setup pedido por $D816 (0 = ninguno): hasta
  // terminarlo, el core ignora las escrituras del CPU a la VRAM.
  int     setup_lineas_ = 0;
  bool    init_arranque_ = false; // el init en curso es el del reset, no un SETUP
  // SOLID_HIT ($D803 bit 5): algún sprite toca una celda con el bit 4 del
  // atributo. Se recalcula entero al entrar en cada blanking.
  bool    solid_hit_ = false;
  // OVERFLOW ($D803 bit 6): la línea física actual o la anterior llegaron a NSL
  // sprites.
  bool    lleno_ = false;
  bool    lleno_previo_ = false;
  // Línea física del barrido (0..524). Arranca en el blanking.
  int     linea_ = V_VISIBLE;

  // VRAM
  std::array<uint8_t, 2048> tilemap_{}; // área 00
  std::array<uint8_t, 2048> attrib_{};  // área 01
  // Patrones de fondo: 256 tiles × 8 filas × 2 planos
  std::array<uint8_t, 256 * 8 * 2> fgpat_{};
  std::array<uint8_t, 160> oam_{};      // 32 sprites × 5 bytes
  // Patrones de sprite: 64 × 8 filas × 2 planos
  std::array<uint8_t, 64 * 8 * 2> sppat_{};

  // Paletas: 32 entradas × 12 bits (RGB444)
  std::array<uint16_t, 32> pal_{};

  // Último frame renderizado (320x240 RGBA8888).
  std::vector<uint32_t> last_frame_;

  // Plano 0 retenido al escribir la fila (manual del core, B.3): uno por banco.
  uint8_t plano0_fondo_ = 0;
  uint8_t plano0_sprite_ = 0;

  // Hueco del line buffer de sprites.
  struct Entrada {
    int x;
    bool scale2x, flip_x, prio;
    uint8_t pal;
    uint8_t p0, p1; // fila del patrón, ya con FLIP_Y aplicado
  };

  // Helpers
  int evaluar_linea(int py, Entrada* lb) const;
  void calcular_bandas(std::array<uint8_t, V_VISIBLE>& bandas, int& fin) const;
  uint8_t componer_status() const;
  void write_vram(uint8_t lo, uint8_t hi, uint8_t data);
  void default_palette();
  uint16_t pal_index(int area, uint8_t attrib_byte, uint8_t color) const;
  // Funciones de color sobre una paleta dada (no sobre pal_).
  uint32_t rgb444_to_rgba(uint16_t rgb444) const;
  void load_default_font();
  // Tiles de fondo 0..7: los escribe la fase 2 del init del core (arte de demo).
  void load_demo_bg_tiles();
  // Patrones de sprite 0..2: los escribe la fase 3 del init del core.
  void load_sprite_pattern(int patron, const uint8_t* p0, const uint8_t* p1);
  void load_demo_sprite_patterns();
  void clear_demo_sprite_patterns();
  void aplicar_setup();
  void barrido_colision();
  void render_tile_row(uint32_t* out, int band, int src_y, int scroll_x, int scroll_y);
};
