#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "terminal/terminal_state.h"

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

  // Estado del terminal (80x24) que refleja la salida del UART. Se copia una
  // vez por fotograma: Main lo actualiza desde Uart::terminal().
  void set_terminal(const TerminalState& term) {
    // La codificacion la elige el usuario en el panel; el estado que llega de
    // main.cpp no lo trae, asi que se conserva el que tenga la ventana.
    const bool u8 = term_.utf8_activo();
    term_ = term;
    term_.set_utf8(u8);
    term_sucio_ = true;  // el estado puede traer sucio=false aunque cambie
  }
  bool has_terminal() const { return terminal_; }

  // Atiende los eventos: cierra la ventana o actualiza el joystick.
  // Devuelve false cuando el usuario cierra la ventana.
  bool poll();

  bool active() const { return active_; }

  // Modos de vista. El panel de depuración solo se ve en el modo Normal.
  enum class Vista {
    Normal,        // panel (LEDs, puerto 1, consola) + vídeo
    SoloVideo,     // sin panel, vídeo grande dentro de la ventana (para jugar)
    PantallaCompleta,  // sin panel y a pantalla completa del escritorio
  };

  // Avanza al siguiente modo: Normal -> SoloVideo -> PantallaCompleta -> Normal.
  void siguiente_vista();
  // Salta a un modo concreto (lo usan los botones del margen).
  void set_vista(Vista v);
  Vista vista() const { return vista_; }
  bool panel_visible() const { return vista_ == Vista::Normal; }

  // Estado actual del joystick en el formato del puerto 1 (activo en bajo).
  uint8_t joy() const { return joy_; }

  // --- Menú SD ---------------------------------------------------------
  //
  // La ventana no sabe gestionar archivos: cuando el usuario elige una acción
  // del menú, se llama a este callback para que main.cpp la ejecute (donde
  // están la tarjeta y la emulación). `accion` es:
  //   "add"    añadir un archivo (se pide con el diálogo nativo)
  //   "list"   mostrar el listado
  //   "delete" borrar (nombre en `arg`)
  //   "remount" re-montar la SD en el monitor (para que el firmware actualice
  //            su caché del directorio)
  std::function<void(const std::string& accion, const std::string& arg)> on_sd_accion;

  // Listado a mostrar en el desplegable (lo rellena main.cpp).
  struct ArchivoSd {
    std::string nombre;
    uint32_t bytes = 0;
  };
  void set_listado_sd(std::vector<ArchivoSd> lista);

private:
  // Celda del terminal: la fuente 8x8 del Core de Vídeo escalada TERM_SCALE.
  // Se deja a x1 (8x8 px, el tamaño nativo de la fuente): es lo que más se
  // parece a una consola real y permite 80 columnas en solo 640 px. Con x2 el
  // texto resulta demasiado grande y se come el ancho del vídeo.
  static constexpr int TERM_SCALE = 1;
  static constexpr int TERM_CW = 8 * TERM_SCALE;   // ancho de celda en píxeles
  static constexpr int TERM_CH = 16 * TERM_SCALE;  // alto de celda (fuente CP437 8x16)
  // Geometría de la consola: 80x24, lo que asumen los programas del sistema.
  static constexpr int TERM_COLS = TerminalState::COLS;
  static constexpr int TERM_PAD = 8;      // margen interior

  // Dibuja la rejilla del terminal dentro de un rectángulo.
  void dibujar_terminal(void* renderer, const void* caja);

  // Módulo SD (lista de archivos con acciones). Rellena `zonas_`.
  // Devuelve el alto ocupado.
  int dibujar_modulo_sd(void* renderer, int x, int y);
  // Diálogo de entrada de texto (encima del panel).
  void dibujar_dialogo(void* renderer);

  // Dibuja placa + LEDs + puerto 1 en su textura de caché (solo si cambió el
  // estado). Devuelve la textura lista para copiar en (x, y).
  void* panel_cache(int x, int y, uint8_t leds);

  // Alt+Intro recorre los modos de vista (equivale a F11).
  void poll_alt_enter();

  // Redimensiona la ventana para que el panel quepa entero (el módulo SD crece
  // con el número de archivos y la consola siempre debe verse a 80x24).
  void ajustar_alto_ventana();

  // Rectángulos de los botones del margen del vídeo (pestaña para cambiar de
  // modo). Se calculan en cada fotograma junto con el rectángulo del vídeo.
  void dibujar_boton_vista(void* renderer, int x, int y, int w, int h, bool expandir);
  void clic_en(int x, int y);

  // Ancho mínimo del panel lateral, en píxeles de ventana. El ancho real se
  // calcula en start() para que quepan TERM_COLS columnas de texto; este valor
  // es el mínimo (el que necesita la placa de LEDs).
  static constexpr int PANEL_W_MIN = 300;

  bool active_ = false;
  void* window_ = nullptr;
  void* renderer_ = nullptr;
  void* texture_ = nullptr;
  // Textura de la consola: se pinta en un buffer y se sube de una vez, en vez
  // de dibujar miles de rectangulos por fotograma (era el cuello de botella).
  void* term_tex_ = nullptr;
  std::vector<uint32_t> term_px_;  // buffer 640x384 de la consola
  bool term_sucio_ = true;         // hay que regenerar term_px_

  // Cache del panel de estado (placa + LEDs + puerto 1) en una textura: solo
  // se redibuja cuando cambian los LEDs, los pines o los colores con que se
  // pinta según el modo de cada bit.
  void* panel_tex_ = nullptr;
  int panel_tex_w_ = 0, panel_tex_h_ = 0;
  uint8_t cache_leds_ = 0xFF;
  uint8_t cache_p1v_ = 0xFF;
  uint8_t cache_p1d_ = 0xFF;
  bool cache_modo_ = false;     // hubo algún bit como entrada (colores distintos)
  bool cache_valida_ = false;

  // Módulo SD del panel (lista de archivos con acciones).
  //
  // Diálogo de entrada de texto propio (SDL no trae uno): lo usan renombrar y
  // "añadir con nombre". Mientras está activo, el teclado va al campo.
  struct Dialogo {
    bool activo = false;
    bool seleccionado = false;  // el texto inicial se sustituye al teclear
    std::string titulo;
    std::string texto;
    std::string accion;   // qué hay que hacer al aceptar
    std::string arg;      // contexto (p. ej. el nombre viejo)
  };
  Dialogo dialogo_;

  // Geometría del módulo SD.
  static constexpr int FILA_SD = 18;    // alto de cada fila de archivo
  static constexpr int BOTON_SD = 34;   // ancho de los botones REN/DEL
  std::vector<ArchivoSd> listado_sd_;
  // Zonas clicables del menú, en coordenadas del renderer.
  struct Zona { int x, y, w, h; std::string accion; std::string arg; };
  std::vector<Zona> zonas_;
  int fila_resaltada_ = -1;   // archivo bajo el ratón
  int fila_borrar_ = -1;      // archivo pendiente de confirmar borrado
  int alto_modulo_sd_ = 0;    // alto actual del módulo (para la ventana)
  int alto_ventana_previo_ = 0;  // último alto pedido en ajustar_alto_ventana()
  int w_ = 320;
  int h_ = 240;
  int scale_ = 3;
  int panel_w_ = 0;               // ancho del panel lateral (0 = sin panel)
  bool terminal_ = false;         // módulo del terminal activado
  Vista vista_ = Vista::Normal;   // modo de vista actual
  // Último estado del puerto 1 (lo pone main.cpp en cada fotograma).
  uint8_t port1_valor_ = 0xF8;
  uint8_t port1_dir_ = 0x00;
  // Geometría de la ventana antes de pasar a pantalla completa, para
  // restaurarla al salir.
  int win_x_ = 0, win_y_ = 0, win_w_ = 0, win_h_ = 0;
  // Rectángulo del botón de cambio de vista, en coordenadas de ventana. Se
  // recalcula al dibujar; el clic lo consulta.
  int boton_x_ = 0, boton_y_ = 0, boton_w_ = 0, boton_h_ = 0;
  TerminalState term_;            // estado del terminal (80x24)
  uint8_t joy_ = 0xF8; // en reposo: todo a 1 (activo en bajo)
  // Estado de cada fuente por separado, para combinarlas sin que una borre a
  // la otra (ver la nota en emu_window.cpp).
  uint8_t joy_teclado_ = 0xF8;
  uint8_t joy_mando_ = 0xF8;
};
