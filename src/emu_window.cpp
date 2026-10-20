#include "emu_window.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gpio_uart.h"

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

#include "video/font_c64.h"

EmuWindow::~EmuWindow() {
#ifdef HAVE_SDL2
  if (texture_) SDL_DestroyTexture(static_cast<SDL_Texture*>(texture_));
  if (renderer_) SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
  if (window_) SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
#endif
}

namespace {

#ifdef HAVE_SDL2

// Rectángulo relleno con esquinas redondeadas (para la placa y los LEDs).
void fill_round_rect(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
  SDL_Rect mid = {rc.x + radius, rc.y, rc.w - 2 * radius, rc.h};
  SDL_Rect l = {rc.x, rc.y + radius, radius, rc.h - 2 * radius};
  SDL_Rect rr = {rc.x + rc.w - radius, rc.y + radius, radius, rc.h - 2 * radius};
  SDL_RenderFillRect(r, &mid);
  SDL_RenderFillRect(r, &l);
  SDL_RenderFillRect(r, &rr);
  for (int dy = 0; dy < radius; dy++) {
    for (int dx = 0; dx < radius; dx++) {
      const float d = std::sqrt((float)(radius - dx) * (radius - dx) +
                                (float)(radius - dy) * (radius - dy));
      if (d > radius) continue;
      SDL_RenderDrawPoint(r, rc.x + radius - dx - 1, rc.y + dy);
      SDL_RenderDrawPoint(r, rc.x + rc.w - radius + dx, rc.y + dy);
      SDL_RenderDrawPoint(r, rc.x + radius - dx - 1, rc.y + rc.h - dy - 1);
      SDL_RenderDrawPoint(r, rc.x + rc.w - radius + dx, rc.y + rc.h - dy - 1);
    }
  }
}

// Círculo relleno (para los LEDs).
void fill_circle(SDL_Renderer* r, int cx, int cy, int radius) {
  for (int dy = -radius; dy <= radius; dy++) {
    const int span = (int)std::sqrt((float)(radius * radius - dy * dy));
    SDL_RenderDrawLine(r, cx - span, cy + dy, cx + span, cy + dy);
  }
}

// LED con halo tipo "glow" cuando está encendido.
void draw_led(SDL_Renderer* r, int cx, int cy, int radius, bool on) {
  if (on) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int k = 3; k >= 1; k--) {
      SDL_SetRenderDrawColor(r, 60, 255, 140, (Uint8)(28 * k));
      fill_circle(r, cx, cy, radius + k * 2);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 120, 255, 170, 255);
    fill_circle(r, cx, cy, radius);
    SDL_SetRenderDrawColor(r, 235, 255, 240, 255);
    fill_circle(r, cx - radius / 3, cy - radius / 3, radius / 3);
  } else {
    SDL_SetRenderDrawColor(r, 26, 44, 34, 255);
    fill_circle(r, cx, cy, radius);
    SDL_SetRenderDrawColor(r, 16, 28, 22, 255);
    fill_circle(r, cx, cy, radius - 2);
  }
}

// --- Terminal del UART --------------------------------------------------------
//
// SDL2 no dibuja texto, así que se reutiliza la fuente 8x8 del Core de Vídeo
// (src/video/font_c64.h) y se pinta glifo a glifo, escalada x2 (16x16 px por
// carácter). El panel es de solo lectura: muestra lo mismo que sale por el
// socket, para depurar sin cambiar a PuTTY.
//
// TERM_CW/TERM_CH/TERM_COLS/TERM_PAD viven en emu_window.h: start() los usa
// para dimensionar el panel.

// Separación entre el panel lateral y la zona del vídeo (pantalla completa).
static constexpr int VIDEO_PAD = 16;

void draw_glyph(SDL_Renderer* r, int x, int y, char c) {
  // La fuente cubre ASCII desde $20. Lo no imprimible se pinta como espacio.
  const int idx = ((unsigned char)c) - 0x20;
  if (idx < 0 || idx >= 96) return;
  for (int row = 0; row < 8; row++) {
    const uint8_t bits = kFontC64[idx * 8 + row];
    if (!bits) continue;
    for (int col = 0; col < 8; col++) {
      if (!(bits & (0x80 >> col))) continue;
      SDL_Rect px{x + col * 2, y + row * 2, 2, 2};
      SDL_RenderFillRect(r, &px);
    }
  }
}

// --- Fuentes de entrada -------------------------------------------------------
//
// Teclado y mando se mantienen SEPARADOS y se combinan poniendo a 0 (pulsado,
// lógica activa en bajo) si CUALQUIER fuente lo pide. Si ambos escribieran la
// misma variable, soltar una tecla apagaría una dirección que se está
// sosteniendo con el mando, y el temblor de un stick en reposo borraría las
// pulsaciones del teclado.
struct Entrada {
  uint8_t teclado = Gpio::JOY_IDLE; // bits 3-7; 0 = pulsado
  uint8_t mando = Gpio::JOY_IDLE;

  void set(uint8_t& fuente, uint8_t bit, bool pulsado) {
    if (pulsado) fuente &= (uint8_t)~bit;
    else         fuente |= bit;
  }

  uint8_t combinado() const { return (uint8_t)(teclado & mando); }
};

#endif

} // namespace

bool EmuWindow::start(int video_w, int video_h, int scale, bool terminal) {
#ifdef HAVE_SDL2
  w_ = video_w;
  h_ = video_h;
  scale_ = scale;
  terminal_ = terminal;
  // Panel lateral: presente si hay algo que poner en él (ahora, el terminal).
  // El ancho se ajusta para que quepan TERM_COLS columnas de texto: es lo que
  // se compara con PuTTY, así que la consola manda sobre la placa de LEDs.
  panel_w_ = 0;
  if (terminal_) {
    const int necesario = TERM_COLS * TERM_CW + 2 * 8 + TERM_PAD;
    panel_w_ = necesario > PANEL_W_MIN ? necesario : PANEL_W_MIN;
  }

  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "video: SDL_InitSubSystem fallo: %s\n", SDL_GetError());
    return false;
  }

  // Reparto del ancho de pantalla entre panel y vídeo.
  //
  // Prioridad: que la consola muestre 80 columnas (lo comparable con PuTTY)
  // sin comerse el vídeo. Si no caben las dos cosas, primero se baja la
  // escala del vídeo a ×2 y después, si aún no cabe, se estrecha la consola
  // hasta el mínimo de la placa. (Después de SDL_InitSubSystem: antes no hay
  // modo de vídeo consultable.)
  {
    SDL_Rect usable{0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 &&
        usable.w > 0 && usable.h > 0) {
      // Margen para el borde de la ventana y el redondeo del DPI.
      const int margen = 40;
      while (scale_ > 2 &&
             (w_ * scale_ + panel_w_ + margen > usable.w ||
              h_ * scale_ + 120 > usable.h)) {
        scale_--;
      }
      if (terminal_) {
        const int libre = usable.w - w_ * scale_ - margen;
        if (libre < panel_w_) {
          // Se recortan columnas completas, no píxeles a medias: así el texto
          // se corta siempre por una columna entera.
          int cols = (libre - 2 * 8 - TERM_PAD) / TERM_CW;
          if (cols < 24) cols = 24;
          panel_w_ = cols * TERM_CW + 2 * 8 + TERM_PAD;
          if (panel_w_ < PANEL_W_MIN) panel_w_ = PANEL_W_MIN;
        }
      }
    }
  }
  // Mandos del PC: si no hay ninguno, SDL simplemente no genera eventos. Se
  // inicializa aparte para que un fallo no impida abrir la ventana.
  if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "mando: sin soporte de gamepads: %s\n", SDL_GetError());
  } else {
    // Los mandos ya conectados se abren solos: SDL envía un evento
    // CONTROLLERDEVICEADDED por cada uno al empezar el bucle de eventos (y si
    // se abre aquí también, aparecería dos veces en el log).
    std::printf("Mandos disponibles: %d\n", SDL_NumJoysticks());
  }

  SDL_Window* win = SDL_CreateWindow(
      "Emulador FPGA 6502 - Tang Nano 9K",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      w_ * scale_ + panel_w_, h_ * scale_,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!win) {
    std::fprintf(stderr, "video: SDL_CreateWindow fallo: %s\n", SDL_GetError());
    return false;
  }
  // Tamano minimo: el panel lateral y el video a la escala calculada. Asi el
  // boton de maximizar (y el arrastre de bordes) siempre amplian el video y
  // nunca aplastan el panel.
  SDL_SetWindowMinimumSize(win, w_ * scale_ + panel_w_, h_ * scale_);

  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) {
    std::fprintf(stderr, "video: SDL_CreateRenderer fallo: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    return false;
  }

  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, w_, h_);
  if (!tex) {
    std::fprintf(stderr, "video: SDL_CreateTexture fallo: %s\n", SDL_GetError());
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return false;
  }

  window_ = win;
  renderer_ = ren;
  texture_ = tex;
  active_ = true;
  joy_ = Gpio::JOY_IDLE;
  // Arranque directo en pantalla completa (útil para lanzadores y pruebas:
  // EMU_FULLSCREEN=1). F11/Alt+Intro lo alternan en marcha.
  if (std::getenv("EMU_FULLSCREEN")) toggle_fullscreen();
  std::printf("Video: ventana %dx%d (escala %dx)%s\n", w_, h_, scale_,
              terminal_ ? " + panel lateral: LEDs y terminal UART" : "");
  std::printf("Joystick (puerto 1): cursores o WASD para mover, Espacio/Z/Ctrl para fuego."
              " También sirve un mando conectado al PC.\n");
  return true;
#else
  (void)video_w;
  (void)video_h;
  (void)scale;
  std::printf("Video: sin SDL2, ventana desactivada\n");
  return false;
#endif
}

void EmuWindow::set_terminal(const std::string& lineas) {
  // Se parte el texto en renglones y se guardan los últimos que pueden verse.
  // El texto del UART ya viene con '\n' como separador (ver Uart::tap_push).
  term_lineas_.clear();
  size_t ini = 0;
  while (ini <= lineas.size()) {
    size_t fin = lineas.find('\n', ini);
    if (fin == std::string::npos) fin = lineas.size();
    term_lineas_.emplace_back(lineas, ini, fin - ini);
    if (fin == lineas.size()) break;
    ini = fin + 1;
  }
  // Tope: solo interesan las últimas.
  const size_t tope = 200;
  if (term_lineas_.size() > tope) {
    term_lineas_.erase(term_lineas_.begin(),
                       term_lineas_.end() - (std::ptrdiff_t)tope);
  }
}

void EmuWindow::toggle_fullscreen() {
#ifdef HAVE_SDL2
  if (!active_ || !window_) return;
  SDL_Window* win = static_cast<SDL_Window*>(window_);
  fullscreen_ = !fullscreen_;
  if (fullscreen_) {
    // Se guarda la geometría para restaurarla al salir.
    SDL_GetWindowPosition(win, &win_x_, &win_y_);
    SDL_GetWindowSize(win, &win_w_, &win_h_);
    SDL_Rect usable{0, 0, 0, 0};
    SDL_GetDisplayUsableBounds(0, &usable);
    SDL_SetWindowBordered(win, SDL_FALSE);
    SDL_SetWindowPosition(win, usable.x, usable.y);
    SDL_SetWindowSize(win, usable.w, usable.h);
    if (SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
      std::fprintf(stderr, "video: pantalla completa fallo: %s\n", SDL_GetError());
    }
  } else {
    SDL_SetWindowFullscreen(win, 0);
    SDL_SetWindowBordered(win, SDL_TRUE);
    SDL_SetWindowSize(win, win_w_, win_h_);
    SDL_SetWindowPosition(win, win_x_, win_y_);
  }
  std::printf("Video: %s\n", fullscreen_ ? "pantalla completa (F11 para salir)" : "ventana");
  std::fflush(stdout);
#endif
}

void EmuWindow::poll_alt_enter() {
#ifdef HAVE_SDL2
  // Se detecta por estado de modificadores en vez de por evento para que
  // funcione igual con cualquiera de los dos Alt.
  static bool alt_enter_was = false;
  const Uint8* ks = SDL_GetKeyboardState(nullptr);
  const bool enter = ks[SDL_SCANCODE_KP_ENTER] || ks[SDL_SCANCODE_RETURN];
  const bool alt = (ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT]) != 0;
  const bool now = alt && enter;
  // No basta con `now`: con Alt pulsado, Intro también dispara el fuego en
  // este emulador, así que se exige flanco y Alt para no duplicar.
  if (now && !alt_enter_was) toggle_fullscreen();
  alt_enter_was = now;
#endif
}

void EmuWindow::present(const uint32_t* rgb, uint8_t leds) {
#ifdef HAVE_SDL2
  if (!active_) return;
  SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer_);
  SDL_Texture* tex = static_cast<SDL_Texture*>(texture_);

  SDL_UpdateTexture(tex, nullptr, rgb, w_ * (int)sizeof(uint32_t));

  // Fondo general del panel (gris oscuro tipo escritorio).
  SDL_SetRenderDrawColor(ren, 28, 30, 34, 255);
  SDL_RenderClear(ren);

  // --- Vídeo (columna derecha) ---
  //
  // En ventana va a su escala normal (×scale_, 1 px de emulador = N px).
  // Si el usuario agranda la ventana a mano o la maximiza (botón de Windows),
  // el panel lateral NO cambia: es el vídeo el que se amplía hasta llenar el
  // sitio. Igual a pantalla completa.
  int out_w = 0, out_h = 0;
  SDL_GetRendererOutputSize(ren, &out_w, &out_h);

  SDL_Rect vdst;
  {
    const int available_w = out_w - panel_w_ - VIDEO_PAD;
    const int available_h = out_h;
    const int base_w = w_ * scale_;
    const int base_h = h_ * scale_;
    if (fullscreen_ || available_w > base_w + 8 || available_h > base_h + 8) {
      // Hay sitio de sobra: se escala al mayor factor entero que quepa (píxeles
      // cuadrados, como en el FPGA) y se centra en la zona libre.
      int f = available_w / w_;
      if (f * h_ > available_h) f = available_h / h_;
      if (f < 1) {
        // No cabe a escala entera: se estira manteniendo la proporción 4:3.
        const double fx = (double)available_w / w_;
        const double fy = (double)available_h / h_;
        const double fmin = fx < fy ? fx : fy;
        const int vw = (int)(w_ * fmin);
        const int vh = (int)(h_ * fmin);
        vdst = SDL_Rect{panel_w_ + VIDEO_PAD + (available_w - vw) / 2,
                        (available_h - vh) / 2, vw, vh};
      } else {
        const int vw = w_ * f;
        const int vh = h_ * f;
        vdst = SDL_Rect{panel_w_ + VIDEO_PAD + (available_w - vw) / 2,
                        (available_h - vh) / 2, vw, vh};
      }
    } else {
      vdst = SDL_Rect{panel_w_, 0, base_w, base_h};
    }
  }
  SDL_RenderCopy(ren, tex, nullptr, &vdst);

  // Alto útil del panel: el de la ventana (o el de la pantalla a fullscreen).
  const int area_h = out_h > 0 ? out_h : h_ * scale_;

  // --- Panel lateral izquierdo ---
  //
  // Se organiza en módulos apilados de arriba abajo: cada uno es un bloque
  // con su título y su propio contenido. Para añadir uno nuevo basta con
  // calcular su rectángulo a partir de `y` y restar su alto, que es lo que se
  // hace aquí con la placa de LEDs y el terminal.
  if (panel_w_ > 0) {
    SDL_Rect panel{0, 0, panel_w_, area_h};
    SDL_SetRenderDrawColor(ren, 34, 37, 43, 255);
    SDL_RenderFillRect(ren, &panel);

    // Separador vertical entre el panel y el vídeo.
    SDL_SetRenderDrawColor(ren, 52, 57, 66, 255);
    SDL_RenderDrawLine(ren, panel_w_ - 1, 0, panel_w_ - 1, area_h);

    int y = 12; // cursor vertical dentro del panel

    // --- Módulo 1: placa Tang Nano 9K con sus 6 LEDs ---
    const int board_w = 6 * 30 + 34;
    const int board_h = 46;
    const int board_x = (panel_w_ - board_w) / 2;
    SDL_Rect board{board_x, y, board_w, board_h};
    fill_round_rect(ren, board, 8, SDL_Color{18, 40, 30, 255});
    SDL_SetRenderDrawColor(ren, 30, 70, 52, 255);
    SDL_RenderDrawRect(ren, &board);

    const int radius = 7;
    const int spacing = 30;
    const int first_x = board_x + 26;
    const int cy = y + board_h / 2;
    for (int i = 0; i < 6; i++) {
      draw_led(ren, first_x + i * spacing, cy, radius, (leds >> i) & 1);
    }
    y += board_h + 14;

    // --- Módulo 2: terminal del UART ---
    if (terminal_) {
      const int alto_total = area_h;
      const int term_h = alto_total - y - 12; // hasta el borde inferior
      const int term_x = 8;
      const int term_w = panel_w_ - 16;
      SDL_Rect term{term_x, y, term_w, term_h};
      SDL_SetRenderDrawColor(ren, 10, 14, 12, 255);
      SDL_RenderFillRect(ren, &term);
      SDL_SetRenderDrawColor(ren, 30, 48, 38, 255);
      SDL_RenderDrawRect(ren, &term);

      // Se dibujan los últimos renglones que caben, alineados abajo: lo más
      // reciente queda siempre visible, como en un terminal real.
      const int visibles = (term_h - TERM_PAD) / TERM_CH;
      const int total = (int)term_lineas_.size();
      const int desde = total > visibles ? total - visibles : 0;
      const int maxc = (term_w - TERM_PAD) / TERM_CW;
      SDL_SetRenderDrawColor(ren, 180, 220, 180, 255);
      int ly = y + TERM_PAD / 2;
      for (int i = desde; i < total; i++) {
        const std::string& linea = term_lineas_[(size_t)i];
        int lx = term_x + TERM_PAD / 2;
        for (int c = 0; c < (int)linea.size() && c < maxc; c++) {
          draw_glyph(ren, lx, ly, linea[(size_t)c]);
          lx += TERM_CW;
        }
        ly += TERM_CH;
      }
    }
  }

  SDL_RenderPresent(ren);
#endif
}

bool EmuWindow::poll() {
#ifdef HAVE_SDL2
  if (!active_) return true;

  // Alt+Intro tambien alterna pantalla completa (costumbre en visores).
  // SDL_GetModState se consulta al vuelo para no depender del orden de eventos.
  poll_alt_enter();

  // Se parte del estado combinado anterior y se actualiza con los eventos de
  // esta vuelta. Así no se pierden pulsaciones que sigan activas.
  static Entrada in;
  in.teclado = joy_teclado_;
  in.mando = joy_mando_;

  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT:
        return false;

      // --- Teclado ---
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        const bool down = (ev.type == SDL_KEYDOWN);
        uint8_t& t = in.teclado;
        switch (ev.key.keysym.sym) {
          // Cruceta: cursores y WASD.
          case SDLK_UP:    case SDLK_w: in.set(t, Gpio::JOY_UP, down);    break;
          case SDLK_DOWN:  case SDLK_s: in.set(t, Gpio::JOY_DOWN, down);  break;
          case SDLK_LEFT:  case SDLK_a: in.set(t, Gpio::JOY_LEFT, down);  break;
          case SDLK_RIGHT: case SDLK_d: in.set(t, Gpio::JOY_RIGHT, down); break;
          // Fuego: Espacio, Z, Ctrl izquierdo o Intro.
          case SDLK_SPACE: case SDLK_z: in.set(t, Gpio::JOY_FIRE, down);     break;
          case SDLK_LCTRL: case SDLK_RETURN: in.set(t, Gpio::JOY_FIRE, down); break;
          // Pantalla completa. Solo afecta a la ventana: el panel lateral
          // conserva su tamaño y es el vídeo el que se amplía.
          case SDLK_F11:
            if (down) toggle_fullscreen();
            break;
          default: break;
        }
        break;
      }

      // --- Mando conectado al PC ---
      case SDL_CONTROLLERDEVICEADDED: {
        // Llega uno por cada mando conectado al arrancar (y al enchufar uno
        // nuevo). En este evento `which` es el índice de dispositivo.
        const int idx = ev.cdevice.which;
        if (SDL_IsGameController(idx) && SDL_GameControllerOpen(idx)) {
          std::printf("Mando: %s\n", SDL_GameControllerNameForIndex(idx));
        }
        break;
      }

      case SDL_CONTROLLERDEVICEREMOVED:
        std::printf("Mando desconectado\n");
        in.mando = Gpio::JOY_IDLE; // al irse, no debe dejar nada pulsado
        break;

      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        const bool down = (ev.type == SDL_CONTROLLERBUTTONDOWN);
        uint8_t& m = in.mando;
        switch (ev.cbutton.button) {
          // Cualquier botón de cara o los hombros = fuego, para que sirva
          // cualquier mando.
          case SDL_CONTROLLER_BUTTON_A:
          case SDL_CONTROLLER_BUTTON_B:
          case SDL_CONTROLLER_BUTTON_X:
          case SDL_CONTROLLER_BUTTON_Y:
          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            in.set(m, Gpio::JOY_FIRE, down);
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_UP:    in.set(m, Gpio::JOY_UP, down);    break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  in.set(m, Gpio::JOY_DOWN, down);  break;
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  in.set(m, Gpio::JOY_LEFT, down);  break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: in.set(m, Gpio::JOY_RIGHT, down); break;
          default: break;
        }
        break;
      }

      case SDL_CONTROLLERAXISMOTION: {
        // Solo se toca el eje que se mueve: si se cambiaran los cuatro bits en
        // cada evento, el ruido de un eje en reposo borraría lo que se está
        // pulsando con el teclado o con el otro eje.
        // Los gatillos van de -32768 (suelto) a 32767 (a fondo).
        const int umbral = 16384;
        const int v = ev.caxis.value;
        uint8_t& m = in.mando;
        switch (ev.caxis.axis) {
          case SDL_CONTROLLER_AXIS_LEFTX:
            in.set(m, Gpio::JOY_LEFT, v < -umbral);
            in.set(m, Gpio::JOY_RIGHT, v > umbral);
            break;
          case SDL_CONTROLLER_AXIS_LEFTY:
            in.set(m, Gpio::JOY_UP, v < -umbral);
            in.set(m, Gpio::JOY_DOWN, v > umbral);
            break;
          case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
          case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            in.set(m, Gpio::JOY_FIRE, v > umbral);
            break;
          default:
            // Ejes derechos / palancas: no se usan.
            break;
        }
        break;
      }

      default:
        break;
    }
  }

  joy_teclado_ = in.teclado;
  joy_mando_ = in.mando;
  joy_ = in.combinado();
#endif
  return true;
}
