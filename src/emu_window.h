#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Ventana del emulador: vídeo del Core de Vídeo + panel lateral de depuración.
//
// Disposición:
//
//   +---------------------------+-------------------------------+
//   | PANEL LATERAL             |                               |
//   |  [ placa + 6 LEDs ]       |      vídeo 320x240 x N        |
//   |  [ terminal del UART  ]   |                               |
//   +---------------------------+-------------------------------+
//
// El panel lateral sirve de "banco de trabajo": hoy lleva la placa (6 LEDs,
// los usa por ejemplo el SID player como VU meter) y el terminal del UART, y
// está pensado para ir añadiendo módulos apilados debajo.
//
// Entrada: simula el joystick Atari (DB9) conectado al puerto 1 del FPGA.
// Admite dos fuentes a la vez:
//   - Teclado: cursores para la cruceta, Espacio/Ctrl izquierdo/Z para el fuego.
//     También el teclado numérico, para mandos raros.
//   - Mandos reales conectados al PC (cualquier gamepad que vea SDL): stick,
//     cruceta y botones 0..3.
//
// Formato de `joy()`: el del puerto 1 del hardware, ACTIVO EN BAJO.
//   bit3 = derecha, bit4 = izquierda, bit5 = abajo, bit6 = arriba,
//   bit7 = fuego. En reposo vale 0xF8 (todo a 1).
//
// Si el emulador se compila sin SDL2, todo queda inactivo y el emulador sigue
// funcionando (sin ventana).
class EmuWindow {
public:
  EmuWindow() = default;
  ~EmuWindow();

  // Abre la ventana. Devuelve false si se pudo (o si no hay SDL2).
  // `terminal` añade el panel lateral con los LEDs y la salida del UART (lo
  // mismo que vería PuTTY). Sin él, la ventana es solo el vídeo.
  bool start(int video_w, int video_h, int scale = 3, bool terminal = false);

  // Dibuja un fotograma. `rgb` son video_w*video_h píxeles en formato 0x00RRGGBB
  // (alfa ignorado). `leds` es el estado de los 6 LEDs en los bits 0-5.
  void present(const uint32_t* rgb, uint8_t leds);

  // Texto a mostrar en el panel del terminal (solo si se abrió con terminal).
  // `lineas` son las últimas líneas de la salida del UART, de la más antigua a
  // la más reciente. Se copia internamente.
  void set_terminal(const std::string& lineas);
  bool has_terminal() const { return terminal_; }

  // Atiende los eventos: cierra la ventana o actualiza el joystick.
  // Devuelve false cuando el usuario cierra la ventana.
  bool poll();

  bool active() const { return active_; }

  // Alterna entre ventana y pantalla completa (F11 o Alt+Intro).
  // El panel lateral mantiene su tamaño; el que se amplía es el vídeo.
  void toggle_fullscreen();
  bool fullscreen() const { return fullscreen_; }

  // Estado actual del joystick en el formato del puerto 1 (activo en bajo).
  uint8_t joy() const { return joy_; }

private:
  // Celda del terminal: la fuente 8x8 del Core de Vídeo escalada x2.
  static constexpr int TERM_CW = 8 * 2;   // ancho de celda en píxeles
  static constexpr int TERM_CH = 8 * 2;   // alto de celda en píxeles
  // Columnas que se muestran: 80, el ancho clásico de una consola de 80x24 y el
  // que usa PuTTY por omisión. El panel se ensancha para que quepan.
  static constexpr int TERM_COLS = 80;
  static constexpr int TERM_PAD = 10;     // margen interior

  // Pantalla completa: el vídeo se ajusta a lo máximo que quepa con la
  // proporción de 320x240; el panel lateral no cambia de tamaño.
  void poll_alt_enter();

  // Ancho mínimo del panel lateral, en píxeles de ventana. El ancho real se
  // calcula en start() para que quepan TERM_COLS columnas de texto; este valor
  // es el mínimo (el que necesita la placa de LEDs).
  static constexpr int PANEL_W_MIN = 400;

  bool active_ = false;
  void* window_ = nullptr;
  void* renderer_ = nullptr;
  void* texture_ = nullptr;
  int w_ = 320;
  int h_ = 240;
  int scale_ = 3;
  int panel_w_ = 0;               // ancho del panel lateral (0 = sin panel)
  bool terminal_ = false;         // módulo del terminal activado
  bool fullscreen_ = false;       // ventana vs pantalla completa
  // Geometría de la ventana antes de pasar a pantalla completa, para
  // restaurarla al salir.
  int win_x_ = 0, win_y_ = 0, win_w_ = 0, win_h_ = 0;
  std::vector<std::string> term_lineas_; // últimos renglones del UART
  uint8_t joy_ = 0xF8; // en reposo: todo a 1 (activo en bajo)
  // Estado de cada fuente por separado, para combinarlas sin que una borre a
  // la otra (ver la nota en emu_window.cpp).
  uint8_t joy_teclado_ = 0xF8;
  uint8_t joy_mando_ = 0xF8;
};
