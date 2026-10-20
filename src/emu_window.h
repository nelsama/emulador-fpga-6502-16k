#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Ventana del emulador: vídeo del Core de Vídeo + LEDs del Tang Nano 9K.
//
// La ventana muestra el framebuffer de 320x240 escalado y, debajo, una tira
// que imita los 6 LEDs de la placa (los usa, por ejemplo, el SID player como VU
// meter).
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

  // Abre la ventana. Devuelve false si no se pudo (o si no hay SDL2).
  // `terminal` añade un panel con la salida del UART (lo que vería PuTTY).
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

  // Estado actual del joystick en el formato del puerto 1 (activo en bajo).
  uint8_t joy() const { return joy_; }

private:
  bool active_ = false;
  void* window_ = nullptr;
  void* renderer_ = nullptr;
  void* texture_ = nullptr;
  int w_ = 320;
  int h_ = 240;
  int scale_ = 3;
  int panel_h_ = 78;
  bool terminal_ = false;         // panel de terminal activado
  std::vector<std::string> term_lineas_; // últimos renglones del UART
  uint8_t joy_ = 0xF8; // en reposo: todo a 1 (activo en bajo)
  // Estado de cada fuente por separado, para combinarlas sin que una borre a
  // la otra (ver la nota en emu_window.cpp).
  uint8_t joy_teclado_ = 0xF8;
  uint8_t joy_mando_ = 0xF8;
};
