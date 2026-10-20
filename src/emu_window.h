#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Ventana del emulador: vídeo del Core de Vídeo + panel lateral de depuración.
//
// Disposición:
//
//   +---------------------------+-------------------------------+
//   | PANEL DE DEPURACIÓN       |                               |
//   |  [ placa + 6 LEDs ]       |                               |
//   |  [ puerto 1 (pines) ]     |      vídeo 320x240 x N        |
//   |  [ terminal del UART  ]   |                               |
//   +---------------------------+-------------------------------+
//
// El panel lateral sirve de "banco de trabajo": los módulos se apilan de
// arriba abajo y se dibujan con un cursor vertical, de modo que añadir uno
// nuevo es dibujarlo en su rectángulo y avanzar ese cursor. Hoy lleva:
//   1. la placa del Tang Nano 9K con sus 6 LEDs (el SID player los usa como
//      VU meter),
//   2. el estado de los pines del puerto 1 (joystick Atari, DB9),
//   3. el terminal del UART.
//
// Todo el panel es de SOLO LECTURA: refleja el estado de la máquina, no se
// escribe nada hacia la emulación desde aquí (la entrada sigue siendo el
// teclado/el mando para el joystick).
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

  // Estado del puerto 1 para el módulo de pines del panel (solo lectura).
  // `valor` es lo que lee la CPU en $C000 y `dir` la máscara de dirección
  // (bit a 1 = entrada). Se usan los mismos datos que muestra /state.txt.
  void set_port1(uint8_t valor, uint8_t dir) {
    port1_valor_ = valor;
    port1_dir_ = dir;
  }

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
  // Celda del terminal: la fuente 8x8 del Core de Vídeo escalada TERM_SCALE.
  // Se deja a x1 (8x8 px, el tamaño nativo de la fuente): es lo que más se
  // parece a una consola real y permite 80 columnas en solo 640 px. Con x2 el
  // texto resulta demasiado grande y se come el ancho del vídeo.
  static constexpr int TERM_SCALE = 1;
  static constexpr int TERM_CW = 8 * TERM_SCALE;   // ancho de celda en píxeles
  static constexpr int TERM_CH = 8 * TERM_SCALE;   // alto de celda en píxeles
  // Columnas que se muestran: 80, el ancho clásico de una consola de 80x24 y el
  // que usa PuTTY por omisión. El panel se ensancha para que quepan.
  static constexpr int TERM_COLS = 80;
  static constexpr int TERM_PAD = 8;      // margen interior

  // Pantalla completa: el vídeo se ajusta a lo máximo que quepa con la
  // proporción de 320x240; el panel lateral no cambia de tamaño.
  void poll_alt_enter();

  // Ancho mínimo del panel lateral, en píxeles de ventana. El ancho real se
  // calcula en start() para que quepan TERM_COLS columnas de texto; este valor
  // es el mínimo (el que necesita la placa de LEDs).
  static constexpr int PANEL_W_MIN = 300;

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
  // Último estado del puerto 1 (lo pone main.cpp en cada fotograma).
  uint8_t port1_valor_ = 0xF8;
  uint8_t port1_dir_ = 0x00;
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
