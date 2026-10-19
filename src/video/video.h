#pragma once

#include <array>
#include <cstdint>

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

  Video();

  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Se llama una vez por línea de barrido: avanza VBLANK en $D803 con la
  // duración exacta del hardware.
  void tick_frame();

  // Avanza el barrido hasta la línea absoluta `linea` (contada desde el
  // arranque, 525 por frame). Lo llama el bucle principal con la línea que
  // corresponde al reloj real, de modo que la frecuencia de frames es la del
  // FPGA (59,94 Hz) y no la del número de vueltas del bucle.
  void sync_scanlines(long long linea);

  // Genera la imagen (RGBA8888 / 320x240) en el buffer del llamador.
  void render(uint32_t* out);

  // Tilemap, atributos y OAM (para el renderer y el debugger).
  const std::array<uint8_t, 2048>& tilemap()   const { return tilemap_; }
  const std::array<uint8_t, 2048>& attrib()    const { return attrib_; }
  const std::array<uint8_t, 160>&   oam()      const { return oam_; }

private:
  // Barrido: última línea simulada desde el arranque (para sync_scanlines).
  long long scanline_ = 0;
#ifdef EMU_VIDEO_TRACE
  bool trace_vblank_ = false; // solo para el diagnóstico de frecuencia
#endif
  // Registros directos ($D800-$D817)
  uint8_t vid_addr_lo_ = 0; // $D800
  uint8_t vid_addr_hi_ = 0; // $D801
  // ($D802 es write-only, dispara la escritura)
  uint8_t status_ = 0;       // $D803 (R)
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

  // --- Instantánea para render() ---------------------------------------
  //
  // render() recorre la pantalla leyendo la VRAM. La CPU sigue ejecutándose
  // entre vuelta y vuelta del bucle principal, así que si escribe mientras se
  // pinta, unas filas salen con el estado viejo y otras con el nuevo: el frame
  // no es coherente consigo mismo (GAME OVER dibujado dos veces, textos
  // partidos, etc.).
  //
  // Se copia todo lo que se necesita AL ENTRAR en render() y se pinta solo de
  // la copia. El frame queda consistente aunque la CPU siga escribiendo.
  struct Snapshot {
    std::array<uint8_t, 2048> tilemap;
    std::array<uint8_t, 2048> attrib;
    std::array<uint8_t, 256 * 8 * 2> fgpat;
    std::array<uint8_t, 64 * 8 * 2> sppat;
    std::array<uint8_t, 160> oam;
    std::array<uint16_t, 32> pal;
    int scroll_x, scroll_y;
    int band2_x, band2_y, band3_x, band3_y;
    uint8_t raster0, raster1;
  };
  Snapshot snap_{};
  void take_snapshot();

  // Plano 0 retenido al escribir la fila (ver manual §2.2)
  uint8_t pending_plane0_ = 0;
  bool    plane0_valid_ = false;

  // Helpers
  void write_vram(uint8_t lo, uint8_t hi, uint8_t data);
  void default_palette();
  uint16_t pal_index(int area, uint8_t attrib_byte, uint8_t color) const;
  // Funciones de color sobre una paleta dada (no sobre pal_).
  uint32_t rgb444_to_rgba(uint16_t rgb444) const;
  void load_default_font();
  void render_tile_row(uint32_t* out, int band, int src_y, int scroll_x, int scroll_y);
};
