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
  if (term_tex_) SDL_DestroyTexture(static_cast<SDL_Texture*>(term_tex_));
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

// Margen lateral de los módulos del panel y caja del terminal.
static constexpr int PANEL_MARGEN = 8;

// Alto de cada fila del módulo de puerto 1 (compacto: 8 filas caben en poco).
static constexpr int FILA_P1 = 14;

// Geometría del panel: alto de la franja de estado (placa + LEDs + puerto 1) y
// del módulo SD para `filas` archivos. Deben coincidir con lo que dibujan
// panel_cache() y dibujar_modulo_sd(), que son quienes colocan la consola.
static constexpr int ALTO_ESTADO_PANEL = 12 + 46 + 6 + (30 + 12 + 8 * FILA_P1 + 4) + 6;
static constexpr int SD_CABECERA = 22;
static constexpr int SD_MARGEN_INFERIOR = 8;

// Alto mínimo del panel: estado + SD + consola 80x24 + márgenes.
int alto_panel(int filas_sd) {
  const int filas = filas_sd < 1 ? 1 : filas_sd;
  return ALTO_ESTADO_PANEL + (SD_CABECERA + filas * 18 + SD_MARGEN_INFERIOR) +
         TerminalState::ROWS * 16 + 2 * 8 + 12;
}

void draw_glyph(SDL_Renderer* r, int x, int y, char c, int f) {
  // La fuente cubre ASCII desde $20. Lo no imprimible se pinta como espacio.
  // `f` es el factor de escala de cada píxel del glifo.
  const int idx = ((unsigned char)c) - 0x20;
  if (idx < 0 || idx >= 96) return;
  for (int row = 0; row < 8; row++) {
    const uint8_t bits = kFontC64[idx * 8 + row];
    if (!bits) continue;
    for (int col = 0; col < 8; col++) {
      if (!(bits & (0x80 >> col))) continue;
      SDL_Rect px{x + col * f, y + row * f, f, f};
      SDL_RenderFillRect(r, &px);
    }
  }
}

// Paleta ANSI de 16 colores, la de consola (VGA). La usan tanto el texto como
// el fondo de cada celda del terminal.
const SDL_Color kAnsi[16] = {
    {0, 0, 0, 255},       {170, 0, 0, 255},     {0, 170, 0, 255},     {170, 85, 0, 255},
    {0, 0, 170, 255},     {170, 0, 170, 255},   {0, 170, 170, 255},   {170, 170, 170, 255},
    {85, 85, 85, 255},    {255, 85, 85, 255},   {85, 255, 85, 255},   {255, 255, 85, 255},
    {85, 85, 255, 255},   {255, 85, 255, 255},  {85, 255, 255, 255},  {255, 255, 255, 255},
};

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
    const int necesario = TERM_COLS * TERM_CW + 2 * PANEL_MARGEN + TERM_PAD;
    panel_w_ = necesario > PANEL_W_MIN ? necesario : PANEL_W_MIN;
  }

  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "video: SDL_InitSubSystem fallo: %s\n", SDL_GetError());
    return false;
  }

  // Reparto del ancho de pantalla entre panel y vídeo.
  //
  // El panel se lleva lo justo para que quepan las 80 columnas de la consola
  // (o menos si la pantalla es estrecha) y TODO el resto es para el vídeo, que
  // se escala al mayor factor entero que quepa. Así el Core de Vídeo ocupa el
  // máximo posible.
  int ancho_ventana = w_ * scale_ + panel_w_;
  int alto_ventana = h_ * scale_;

  {
    SDL_Rect usable{0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 &&
        usable.w > 0 && usable.h > 0) {
      // Margen para el borde de la ventana y el redondeo del DPI.
      const int margen = 40;

      // 1. El panel, como mucho, lo que ocupa el 80% del ancho: el vídeo
      //    siempre debe quedar con sitio.
      const int panel_max = (usable.w - margen) * 8 / 10;
      if (terminal_ && panel_w_ > panel_max) {
        // Se recortan columnas completas, no píxeles a medias.
        int cols = (panel_max - 2 * PANEL_MARGEN - TERM_PAD) / TERM_CW;
        if (cols < 24) cols = 24;
        panel_w_ = cols * TERM_CW + 2 * PANEL_MARGEN + TERM_PAD;
      }

      // 2. La ventana arranca ocupando el ancho de la pantalla (sin pasarse),
      //    para que el vídeo se vea grande desde el principio.
      ancho_ventana = usable.w - margen;
      if (ancho_ventana < w_ * scale_ + panel_w_) ancho_ventana = w_ * scale_ + panel_w_;
      if (ancho_ventana > usable.w) ancho_ventana = usable.w;
    }
  }

  // Alto: el necesario para que quepan el panel y sus 24 filas de consola.
  // Sin esto, la rejilla se corta por abajo cuando el vídeo es bajo.
  // La franja de la placa va fija arriba; la ventana crece lo suyo para que
  // nadie pierda la consola de 24 filas.
  // El módulo SD crece con el número de archivos (máximo 16). La ventana se
  // dimensiona para el caso actual.
  const int panel_alto = terminal_ ? alto_panel((int)listado_sd_.size()) : 0;
  if (alto_ventana < panel_alto) alto_ventana = panel_alto;
  alto_ventana_previo_ = alto_ventana;

  // No pasarse de la pantalla: con la barra de tareas y el DPI escalado, una
  // ventana más alta que el escritorio deja parte del vídeo fuera.
  {
    SDL_Rect usable{0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.h > 0) {
      // 80 px: barra de tareas (~40 con DPI 125%) + barra de título. Necesita
      // margen de sobra porque SDL devuelve el escritorio completo, no el área
      // libre real.
      const int max_alto = usable.h - 100;
      if (max_alto > panel_alto && alto_ventana > max_alto) alto_ventana = max_alto;
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
      ancho_ventana, alto_ventana,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!win) {
    std::fprintf(stderr, "video: SDL_CreateWindow fallo: %s\n", SDL_GetError());
    return false;
  }
  // Tamano minimo: el panel lateral y el video a la escala calculada. Asi el
  // boton de maximizar (y el arrastre de bordes) siempre amplian el video y
  // nunca aplastan el panel.
  // Tamano minimo: el panel lateral entero y el video a la escala calculada. El
  // alto minimo es el del panel con la consola completa, no el del arranque: el
  // modulo SD crece y mengua con la tarjeta y no debe recortar la consola.
  const int alto_min = panel_alto > 0 ? panel_alto : alto_ventana;
  SDL_SetWindowMinimumSize(win, panel_w_ + w_ / 2, alto_min);

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
  if (std::getenv("EMU_FULLSCREEN")) set_vista(Vista::PantallaCompleta);
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

void EmuWindow::set_vista(Vista v) {
#ifdef HAVE_SDL2
  if (!active_ || !window_) return;
  if (v == vista_) return;
  SDL_Window* win = static_cast<SDL_Window*>(window_);

  // Al entrar o salir de pantalla completa se guarda/restaura la geometría.
  if (v == Vista::PantallaCompleta && vista_ != Vista::PantallaCompleta) {
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
  } else if (vista_ == Vista::PantallaCompleta) {
    SDL_SetWindowFullscreen(win, 0);
    SDL_SetWindowBordered(win, SDL_TRUE);
    SDL_SetWindowSize(win, win_w_, win_h_);
    SDL_SetWindowPosition(win, win_x_, win_y_);
  }

  vista_ = v;
  const char* nombre = (v == Vista::Normal)            ? "normal (panel + video)"
                       : (v == Vista::SoloVideo)       ? "solo video (ventana)"
                                                       : "solo video (pantalla completa)";
  std::printf("Vista: %s\n", nombre);
  std::fflush(stdout);
#endif
}

void EmuWindow::set_listado_sd(std::vector<ArchivoSd> lista) {
  listado_sd_ = std::move(lista);
  fila_borrar_ = -1;
  // El módulo crece o mengua con la lista; la ventana se ajusta para que la
  // consola siga viéndose entera (24 filas) sin tocar el tamaño del vídeo.
  ajustar_alto_ventana();
}

// La ventana nunca baja del mínimo (el panel completo con la consola 80x24) ni
// sube por encima del escritorio. Si el mínimo no cabe en pantalla, se respeta
// el tamaño de la pantalla y la consola se recorta por abajo antes que el vídeo.
// Se sigue el alto del panel en ambos sentidos: añadir archivos lo hace crecer y
// borrarlos lo encoge, de modo que la consola siempre se ve entera.
void EmuWindow::ajustar_alto_ventana() {
#ifdef HAVE_SDL2
  if (!active_ || !window_ || !terminal_ || vista_ != Vista::Normal) return;
  SDL_Window* win = static_cast<SDL_Window*>(window_);

  int ww = 0, wh = 0;
  SDL_GetWindowSize(win, &ww, &wh);
  int objetivo = alto_panel((int)listado_sd_.size());

  SDL_Rect usable{0, 0, 0, 0};
  if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.h > 0) {
    const int max_alto = usable.h - 100;
    if (objetivo > max_alto) objetivo = max_alto;
  }
  if (objetivo == wh || objetivo == alto_ventana_previo_) return;
  alto_ventana_previo_ = objetivo;
  SDL_SetWindowSize(win, ww, objetivo);
#endif
}

void EmuWindow::siguiente_vista() {
  switch (vista_) {
    case Vista::Normal:           set_vista(Vista::SoloVideo); break;
    case Vista::SoloVideo:        set_vista(Vista::PantallaCompleta); break;
    case Vista::PantallaCompleta: set_vista(Vista::Normal); break;
  }
}

// Pestaña para cambiar de vista. Va en el margen del vídeo, no encima de los
// píxeles del framebuffer: así no tapa lo que se está viendo.
void EmuWindow::dibujar_boton_vista(void* renderer, int x, int y, int w, int h,
                                    bool expandir) {
#ifdef HAVE_SDL2
  SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer);
  boton_x_ = x;
  boton_y_ = y;
  boton_w_ = w;
  boton_h_ = h;

  SDL_Rect r{x, y, w, h};
  fill_round_rect(ren, r, 4, SDL_Color{52, 58, 70, 235});
  SDL_SetRenderDrawColor(ren, 110, 124, 148, 255);
  SDL_RenderDrawRect(ren, &r);

  // Icono: cuatro esquinas (expandir) o dos flechas hacia dentro (volver).
  const int cx = x + w / 2;
  const int cy = y + h / 2;
  SDL_SetRenderDrawColor(ren, 220, 230, 240, 255);
  if (expandir) {
    // Marco con esquinas abiertas hacia fuera.
    const int l = 5, s = 6;
    // esquina sup. izq.
    SDL_RenderDrawLine(ren, cx - l, cy - l, cx - l + s, cy - l);
    SDL_RenderDrawLine(ren, cx - l, cy - l, cx - l, cy - l + s);
    // sup. der.
    SDL_RenderDrawLine(ren, cx + l, cy - l, cx + l - s, cy - l);
    SDL_RenderDrawLine(ren, cx + l, cy - l, cx + l, cy - l + s);
    // inf. izq.
    SDL_RenderDrawLine(ren, cx - l, cy + l, cx - l + s, cy + l);
    SDL_RenderDrawLine(ren, cx - l, cy + l, cx - l, cy + l - s);
    // inf. der.
    SDL_RenderDrawLine(ren, cx + l, cy + l, cx + l - s, cy + l);
    SDL_RenderDrawLine(ren, cx + l, cy + l, cx + l, cy + l - s);
  } else {
    // Flechas hacia dentro: marco completo con puntas.
    const int l = 6;
    SDL_Rect marco{cx - l, cy - l, 2 * l + 1, 2 * l + 1};
    SDL_RenderDrawRect(ren, &marco);
    SDL_RenderDrawLine(ren, cx - l, cy - l, cx - 2, cy - 2);
    SDL_RenderDrawLine(ren, cx + l, cy + l, cx + 2, cy + 2);
  }
#endif
}

// Gestiona un clic en píxeles de ventana (el mismo espacio en que se dibujan
// las zonas clicables).
void EmuWindow::clic_en(int x, int y) {
  // 1) Diálogo de texto abierto: solo responden sus botones (el campo va por
  //    teclado). Se comprueban antes que cualquier otra zona.
  if (dialogo_.activo) {
    for (const Zona& z : zonas_) {
      if (x < z.x || x >= z.x + z.w || y < z.y || y >= z.y + z.h) continue;
      if (z.accion == "dialogo_ok") {
        dialogo_.activo = false;
        if (on_sd_accion) {
          on_sd_accion(dialogo_.accion + "_ok", dialogo_.arg + '\n' + dialogo_.texto);
        }
        return;
      }
      if (z.accion == "dialogo_cancelar") {
        dialogo_.activo = false;
        return;
      }
    }
    return;
  }

  // 2) Módulo SD: las zonas se generan al dibujar (de la última vuelta).
  if (panel_visible()) {
    for (const Zona& z : zonas_) {
      if (x < z.x || x >= z.x + z.w || y < z.y || y >= z.y + z.h) continue;
      if (z.accion == "borrar_pedir") {
        // Primer clic: pide confirmación (la fila se pinta en rojo como SEG).
        for (size_t i = 0; i < listado_sd_.size(); i++) {
          if (listado_sd_[i].nombre == z.arg) fila_borrar_ = (int)i;
        }
      } else if (z.accion == "borrar_confirmar") {
        fila_borrar_ = -1;
        if (on_sd_accion) on_sd_accion("delete", z.arg);
      } else if (z.accion == "codificacion") {
        // Conmuta CP437 <-> UTF-8 en caliente. El panel NO decodifica: solo
        // avisa, porque quien interpreta los bytes del cable es el terminal
        // del UART. Cambiarlo solo aqui haria que el panel mostrara algo
        // distinto de lo que ya decodifico el UART.
        const bool nuevo = !term_.utf8_activo();
        if (on_codificacion) on_codificacion(nuevo);
        term_.set_utf8(nuevo);
        term_sucio_ = true;
        std::printf("Terminal: %s\n", nuevo ? "UTF-8" : "CP437");
        std::fflush(stdout);
      } else if (z.accion == "rename_archivo") {
        dialogo_.activo = true;
        dialogo_.titulo = "Nuevo nombre para " + z.arg;
        dialogo_.texto = z.arg;
        dialogo_.seleccionado = true;
        // Se arranca con el nombre completo seleccionado: teclear directamente
        // sustituye lo que hubiera (como en cualquier renombrador).
        dialogo_.accion = "rename";
        dialogo_.arg = z.arg;
      } else {
        fila_borrar_ = -1;
        if (on_sd_accion) on_sd_accion(z.accion, z.arg);
      }
      return;
    }
    // Clic en cualquier otro sitio del panel: se cancela la confirmación.
    if (fila_borrar_ >= 0) {
      fila_borrar_ = -1;
      return;
    }
  }

  // 3) Botón de cambio de vista.
  if (x >= boton_x_ && x < boton_x_ + boton_w_ && y >= boton_y_ &&
      y < boton_y_ + boton_h_) {
    siguiente_vista();
  }
}

// Pinta la rejilla 80x24 del terminal dentro de `caja`, con sus colores.
//
// IMPORTANTE (rendimiento): la rejilla tiene 80x24 = 1920 celdas y cada glifo
// CP437 tiene hasta ~60 píxeles encendidos. Dibujar eso con rectángulos SDL
// eran ~57.000 llamadas por fotograma (3,5 millones por segundo), que se comían
// 3 ms de los 16,7 disponibles y hacían que el audio se quedara sin datos
// (underruns) y el juego fuera a tirones.
//
// Ahora se compone la imagen en un búfer de 640x384 (memoria) y se sube a una
// textura de una vez: trabajo proporcional a los píxeles, no a las llamadas.
void EmuWindow::dibujar_terminal(void* renderer, const void* caja) {
#ifdef HAVE_SDL2
  SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer);
  const SDL_Rect* term = static_cast<const SDL_Rect*>(caja);

  const int ancho = TerminalState::COLS * 8;
  const int alto = TerminalState::ROWS * 16;

  if (!term_tex_) {
    term_tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, ancho, alto);
    if (!term_tex_) return;
    term_px_.assign((size_t)ancho * alto, 0);
    term_sucio_ = true;
  }

  // Solo se recompone si el terminal ha cambiado desde el último fotograma.
  if (term_.sucio() || term_sucio_) {
    term_.limpiar_sucio();
    term_sucio_ = false;
    term_px_.assign((size_t)ancho * alto, 0x000000);

    for (int fila = 0; fila < TerminalState::ROWS; fila++) {
      for (int col = 0; col < TerminalState::COLS; col++) {
        const TerminalState::Celda& celda = term_.celda(fila, col);
        // Color efectivo: el inverso intercambia tinta y fondo.
        int fg = celda.at.fg & 15;
        int bg = celda.at.bg & 15;
        if (celda.at.negrita) fg |= 8;
        if (celda.at.inverso) {
          const int t = fg;
          fg = bg;
          bg = t;
        }
        const SDL_Color& cf = kAnsi[fg];
        const SDL_Color& cb = kAnsi[bg];
        const uint32_t col_fg = ((uint32_t)cf.r << 16) | ((uint32_t)cf.g << 8) | cf.b;
        const uint32_t col_bg = ((uint32_t)cb.r << 16) | ((uint32_t)cb.g << 8) | cb.b;

        // La celda sabe que glifo le toca (Unicode o CP437); el panel no
        // necesita saber de que programa viene.
        const uint8_t* g = term_.glifo_modo(celda);
        uint32_t* dst = term_px_.data() + (size_t)fila * 16 * ancho + (size_t)col * 8;
        for (int y = 0; y < 16; y++) {
          const uint8_t bits = g[y];
          uint32_t* linea = dst + (size_t)y * ancho;
          for (int x = 0; x < 8; x++) {
            linea[x] = (bits & (0x80 >> x)) ? col_fg : col_bg;
          }
        }
      }
    }
    SDL_UpdateTexture(static_cast<SDL_Texture*>(term_tex_), nullptr,
                      term_px_.data(), ancho * (int)sizeof(uint32_t));
  }

  SDL_RenderCopy(ren, static_cast<SDL_Texture*>(term_tex_), nullptr, term);

  // Cursor: bloque hueco (marco de 1 px) en la posición actual, si está visible.
  if (term_.cursor_visible()) {
    const int x0 = term->x + TERM_PAD / 2;
    const int y0 = term->y + TERM_PAD / 2;
    SDL_Rect c{x0 + term_.cursor_col() * 8, y0 + term_.cursor_fila() * 16, 8, 16};
    SDL_SetRenderDrawColor(ren, 220, 235, 220, 255);
    SDL_RenderDrawRect(ren, &c);
  }
#endif
}

// Dibuja placa + LEDs + puerto 1 en una textura y la devuelve. Solo repinta si
// cambió algo (LEDs, valor o dirección del puerto 1): antes se redibujaban
// cientos de primitivas SDL en CADA fotograma (~1,5 ms de los 16,7 ms
// disponibles, que se notaban como tirones en Pac-Man).
void* EmuWindow::panel_cache(int x, int y, uint8_t leds) {
#ifdef HAVE_SDL2
  SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer_);
  const int alto = ALTO_ESTADO_PANEL;

  if (!panel_tex_ || panel_tex_w_ != panel_w_ || panel_tex_h_ != alto) {
    if (panel_tex_) SDL_DestroyTexture(static_cast<SDL_Texture*>(panel_tex_));
    panel_tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, panel_w_, alto);
    panel_tex_w_ = panel_w_;
    panel_tex_h_ = alto;
    cache_valida_ = false;
  }
  if (!panel_tex_) return nullptr;

  const bool modo = (port1_dir_ & 0xF8) != 0;  // algún bit del joystick de entrada
  const bool cambia = !cache_valida_ || leds != cache_leds_ ||
                      port1_valor_ != cache_p1v_ || port1_dir_ != cache_p1d_ ||
                      modo != cache_modo_;
  if (cambia) {
    cache_valida_ = true;
    cache_leds_ = leds;
    cache_p1v_ = port1_valor_;
    cache_p1d_ = port1_dir_;
    cache_modo_ = modo;

    // El panel se compone en una superficie en memoria con un renderer
    // software, no con una textura de destino del renderer principal: en el
    // backend Direct3D, cambiar el render target a mitad de fotograma descarta
    // lo que ya estaba pintado en el búfer actual (el panel salía en negro).
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
        0, panel_w_, alto, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return panel_tex_;
    SDL_Renderer* sr = SDL_CreateSoftwareRenderer(surf);
    if (!sr) {
      SDL_FreeSurface(surf);
      return panel_tex_;
    }
    ren = sr;  // a partir de aquí se dibuja en la superficie

    SDL_SetRenderDrawColor(ren, 34, 37, 43, 255);
    SDL_RenderClear(ren);

    int y = 12;  // cursor vertical dentro del panel

    // La etiqueta va a la IZQUIERDA de la placa, en el margen del panel (no
    // dentro de la placa ni encima de los LEDs). Si el panel fuese demasiado
    // estrecho para dejarla al lado, se omite antes que solaparla con ellos.
    const int board_w = 6 * 30 + 34;
    const int board_h = 46;
    // El ancho total del módulo incluye la etiqueta a la izquierda: así el
    // bloque queda centrado como un todo y no se desborda del panel.
    const int etiqueta_w = 4 * TERM_CW + 8;
    const int block_w = board_w + etiqueta_w;
    const int block_x = (panel_w_ - block_w) / 2;
    const int board_x = block_x + etiqueta_w;
    SDL_Rect board{board_x, y, board_w, board_h};
    fill_round_rect(ren, board, 8, SDL_Color{18, 40, 30, 255});
    SDL_SetRenderDrawColor(ren, 30, 70, 52, 255);
    SDL_RenderDrawRect(ren, &board);
    if (block_x >= 2) {
      static const char* etiqueta = "LEDS";
      const int ey = y + board_h / 2 - 4;
      const int ex = block_x;
      SDL_SetRenderDrawColor(ren, 120, 165, 140, 255);
      for (int c = 0; c < 4; c++) {
        draw_glyph(ren, ex + c * TERM_CW, ey, etiqueta[c], TERM_SCALE);
      }
    }

    {
      const int radius = 7;
      const int spacing = 30;
      const int first_x = board_x + 26;
      const int cy = y + board_h / 2;
      for (int i = 0; i < 6; i++) {
        draw_led(ren, first_x + i * spacing, cy, radius, (leds >> i) & 1);
      }
    }
    y += board_h + 6;

    // --- Pines del puerto 1 (joystick Atari / DB9) ---
    {
      struct Fila { int bit; int pin; const char* senal; };
      static const Fila filas[8] = {
          {7, 29, "fuego"}, {6, 30, "arriba"}, {5, 33, "abajo"}, {4, 34, "izq"},
          {3, 40, "der"},   {2, 35, "--"},     {1, 41, "--"},    {0, 42, "--"},
      };

      const bool aviso = (port1_dir_ & 0xF8) != 0xF8;
      const int alto_modulo = (aviso ? 30 : 20) + 12 + 8 * FILA_P1 + 4;
      const int mx = PANEL_MARGEN;
      const int mw = panel_w_ - 2 * PANEL_MARGEN;
      SDL_Rect mod{mx, y, mw, alto_modulo};
      fill_round_rect(ren, mod, 6, SDL_Color{24, 28, 34, 255});
      SDL_SetRenderDrawColor(ren, 44, 50, 60, 255);
      SDL_RenderDrawRect(ren, &mod);

      SDL_SetRenderDrawColor(ren, 120, 140, 165, 255);
      const char* titulo = "PUERTO 1 - JOYSTICK (DB9)";
      for (int c = 0; titulo[c]; c++) {
        draw_glyph(ren, mx + 10 + c * TERM_CW, y + 7, titulo[c], TERM_SCALE);
      }

      if (aviso) {
        SDL_SetRenderDrawColor(ren, 230, 170, 90, 255);
        const char* w = "bits 3-7 no son entrada (falta joy_init)";
        const int maxc = (mw - 20) / TERM_CW;
        for (int c = 0; w[c] && c < maxc; c++) {
          draw_glyph(ren, mx + 10 + c * TERM_CW, y + 20, w[c], TERM_SCALE);
        }
      }

      const int y0 = y + (aviso ? 30 : 20);
      SDL_SetRenderDrawColor(ren, 200, 210, 225, 255);
      static const char* kCab = "bit pin senal   modo nivel";
      for (int c = 0; kCab[c]; c++) {
        draw_glyph(ren, mx + 10 + c * TERM_CW, y0, kCab[c], TERM_SCALE);
      }

      const int xcol = mx + 10;
      const int cw = TERM_CW;
      // El panel no se sale de su módulo: con coordenadas negativas (panel
      // estrecho) los rectángulos caerían fuera de la textura y no se verían.
      SDL_RenderSetClipRect(ren, &mod);
      // `port1_valor_` es lo que devuelve la lectura del puerto: con los bits
      // configurados como SALIDA repite lo que escribió la CPU. Para el panel lo
      // útil es el estado de los pines de entrada digital, que es lo que lee un
      // programa que haya hecho joy_init (puerto como entrada).
      const uint8_t entradas = (uint8_t)(port1_valor_ & port1_dir_);
      for (int i = 0; i < 8; i++) {
        const Fila& f = filas[i];
        const int ly = y0 + 14 + i * FILA_P1;
        const bool salida = ((port1_dir_ >> f.bit) & 1) == 0;
        const bool nivel = salida ? ((port1_valor_ >> f.bit) & 1)
                                  : ((entradas >> f.bit) & 1);
        const bool es_joy = f.bit >= 3;

        char linea[64];
        std::snprintf(linea, sizeof linea, "%d   %2d  %-7s %-6s", f.bit, f.pin,
                      f.senal, salida ? "OUT" : "IN");
        if (es_joy) SDL_SetRenderDrawColor(ren, 200, 215, 230, 255);
        else        SDL_SetRenderDrawColor(ren, 110, 120, 135, 255);
        for (int c = 0; linea[c]; c++) {
          draw_glyph(ren, xcol + c * cw, ly, linea[c], TERM_SCALE);
        }

        const int bx = xcol + 23 * cw;
        const int by = ly + 4;
        if (nivel) {
          SDL_SetRenderDrawColor(ren, 90, 220, 140, 255);
          fill_circle(ren, bx, by, 5);
        } else {
          SDL_SetRenderDrawColor(ren, 60, 70, 84, 255);
          fill_circle(ren, bx, by, 5);
          SDL_SetRenderDrawColor(ren, 20, 24, 30, 255);
          fill_circle(ren, bx, by, 3);
        }
      }
      SDL_RenderSetClipRect(ren, nullptr);
    }

    SDL_DestroyRenderer(sr);
    SDL_UpdateTexture(static_cast<SDL_Texture*>(panel_tex_), nullptr, surf->pixels,
                      surf->pitch);
    SDL_FreeSurface(surf);
  }
  (void)x;
  (void)y;
  return panel_tex_;
#else
  (void)x; (void)y; (void)leds;
  return nullptr;
#endif
}

// Módulo SD del panel: cabecera con "+ Anadir" y una fila por archivo con sus
// botones (renombrar / borrar). Alto variable: crece con el número de archivos
// hasta el máximo de la tarjeta (16).
//
// Devuelve el alto que ocupa; present() lo usa para colocar la consola debajo.
int EmuWindow::dibujar_modulo_sd(void* renderer, int x, int y) {
#ifdef HAVE_SDL2
  SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer);
  // Las zonas se regeneran en cada vuelta: `present()` limpia la lista una vez
  // por fotograma, antes de dibujar el módulo y el diálogo.
  const int n = (int)listado_sd_.size();
  const int filas = n < 1 ? 1 : n;   // con la tarjeta vacía, una fila de aviso
  const int cab = 22;                // cabecera con el botón
  const int alto = cab + filas * FILA_SD + 8;

  SDL_Rect mod{x, y, panel_w_ - 2 * PANEL_MARGEN, alto};
  fill_round_rect(ren, mod, 6, SDL_Color{24, 28, 34, 255});
  SDL_SetRenderDrawColor(ren, 44, 50, 60, 255);
  SDL_RenderDrawRect(ren, &mod);

  auto texto = [&](int tx, int ty, const std::string& t, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    for (size_t i = 0; i < t.size(); i++) {
      draw_glyph(ren, tx + (int)i * TERM_CW, ty, t[i], TERM_SCALE);
    }
  };

  // Cabecera: título y botones "Remontar" y "+ Anadir".
  texto(x + 10, y + 5, "SD", SDL_Color{120, 140, 165, 255});
  {
    const int bw = 9 * TERM_CW + 12;
    SDL_Rect b{x + mod.w - bw - 8, y + 3, bw, 16};
    SDL_SetRenderDrawColor(ren, 44, 78, 58, 255);
    SDL_RenderFillRect(ren, &b);
    SDL_SetRenderDrawColor(ren, 90, 200, 130, 255);
    SDL_RenderDrawRect(ren, &b);
    texto(b.x + 6, b.y + 1, "+ Anadir", SDL_Color{190, 240, 205, 255});
    zonas_.push_back(Zona{b.x, b.y, b.w, b.h, "add", ""});

    // El monitor del FPGA cachea el directorio al hacer `SD`: tras tocar la
    // imagen hay que mandarle `sd\r` por el UART para que lo relea. Se hace
    // solo tras cada cambio, pero el boton queda por si se toca la tarjeta
    // por fuera (herramientas, otro emulador, la propia CPU).
    const int rw = 8 * TERM_CW + 12;
    SDL_Rect r{b.x - rw - 6, y + 3, rw, 16};
    SDL_SetRenderDrawColor(ren, 46, 58, 84, 255);
    SDL_RenderFillRect(ren, &r);
    SDL_SetRenderDrawColor(ren, 110, 150, 220, 255);
    SDL_RenderDrawRect(ren, &r);
    texto(r.x + 6, r.y + 1, "Remontar", SDL_Color{200, 220, 250, 255});
    zonas_.push_back(Zona{r.x, r.y, r.w, r.h, "remount", ""});
  }

  if (n == 0) {
    texto(x + 10, y + cab + 2, "(vacia)", SDL_Color{130, 140, 155, 255});
  }

  for (int i = 0; i < n; i++) {
    const ArchivoSd& a = listado_sd_[(size_t)i];
    const int fy = y + cab + i * FILA_SD;
    SDL_Rect fila{x + 4, fy, mod.w - 8, FILA_SD};
    if (i == fila_resaltada_) {
      SDL_SetRenderDrawColor(ren, 44, 50, 62, 255);
      SDL_RenderFillRect(ren, &fila);
    }
    texto(x + 10, fy + 2, a.nombre, SDL_Color{200, 212, 228, 255});
    char tam[24];
    std::snprintf(tam, sizeof tam, "%u", a.bytes);
    texto(x + 10 + 14 * TERM_CW, fy + 2, tam, SDL_Color{130, 142, 160, 255});

    // Botones de la fila. Se dibujan con texto porque la fuente CP437 no
    // tiene iconos: REN (renombrar) y DEL/SEG (borrar / confirmar borrado).
    const int bx = x + mod.w - 14 - 2 * (BOTON_SD + 4);
    {
      SDL_Rect b{bx, fy + 1, BOTON_SD, FILA_SD - 2};
      SDL_SetRenderDrawColor(ren, 56, 66, 86, 255);
      SDL_RenderFillRect(ren, &b);
      texto(b.x + 3, b.y + 1, "REN", SDL_Color{200, 215, 235, 255});
      zonas_.push_back(Zona{b.x, b.y, b.w, b.h, "rename_archivo", a.nombre});
    }
    {
      SDL_Rect b{bx + BOTON_SD + 4, fy + 1, BOTON_SD, FILA_SD - 2};
      const bool confirmar = (fila_borrar_ == i);
      SDL_SetRenderDrawColor(ren, confirmar ? 130 : 56, confirmar ? 44 : 66,
                             confirmar ? 44 : 86, 255);
      SDL_RenderFillRect(ren, &b);
      texto(b.x + 3, b.y + 1, confirmar ? "SEG" : "DEL",
            confirmar ? SDL_Color{255, 210, 210, 255}
                      : SDL_Color{230, 200, 200, 255});
      zonas_.push_back(Zona{b.x, b.y, b.w, b.h,
                            confirmar ? "borrar_confirmar" : "borrar_pedir",
                            a.nombre});
    }
  }

  alto_modulo_sd_ = alto;
  return alto;
#else
  (void)renderer;
  (void)x;
  (void)y;
  return 0;
#endif
}

// Diálogo de entrada de texto (SDL no trae uno). Se dibuja sobre el panel:
// Enter acepta, Esc cancela y Retroceso borra.
void EmuWindow::dibujar_dialogo(void* renderer) {
#ifdef HAVE_SDL2
  if (!dialogo_.activo) return;
  SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer);

  auto texto = [&](int tx, int ty, const std::string& t, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    for (size_t i = 0; i < t.size(); i++) {
      draw_glyph(ren, tx + (int)i * TERM_CW, ty, t[i], TERM_SCALE);
    }
  };

  const int w = panel_w_ - 40;
  const int h = 74;
  const int x = 20;
  // Se coloca sobre el módulo SD (que es de quien parten las acciones) y pegado
  // a su borde superior: así no tapa el puerto 1 y los botones quedan cerca de
  // la fila que se está editando.
  int y = ALTO_ESTADO_PANEL + 4;
  SDL_Rect caja{x, y, w, h};
  SDL_SetRenderDrawColor(ren, 40, 44, 54, 255);
  SDL_RenderFillRect(ren, &caja);
  SDL_SetRenderDrawColor(ren, 120, 134, 160, 255);
  SDL_RenderDrawRect(ren, &caja);

  texto(x + 10, y + 8, dialogo_.titulo, SDL_Color{210, 220, 235, 255});

  SDL_Rect campo{x + 10, y + 28, w - 20, 20};
  SDL_SetRenderDrawColor(ren, 18, 20, 26, 255);
  SDL_RenderFillRect(ren, &campo);
  SDL_SetRenderDrawColor(ren, 90, 105, 130, 255);
  SDL_RenderDrawRect(ren, &campo);
  texto(campo.x + 6, campo.y + 2, dialogo_.texto + "_", SDL_Color{235, 240, 250, 255});
  if (dialogo_.seleccionado) {
    // Marca del texto seleccionado (que se sustituye al teclear).
    const int tw = (int)dialogo_.texto.size() * TERM_CW;
    SDL_SetRenderDrawColor(ren, 90, 120, 190, 255);
    SDL_RenderDrawLine(ren, campo.x + 6, campo.y + 17, campo.x + 6 + tw, campo.y + 17);
  }

  texto(x + 10, y + 54, "Enter=aceptar  Esc=cancelar",
        SDL_Color{140, 152, 170, 255});

  // Botones con el ratón: el teclado no siempre llega (y no hace falta para
  // nada más de la ventana). Las zonas se buscan ANTES que las del módulo SD.
  {
    const int bw = 7 * TERM_CW + 16;
    const int by = y + h - 24;
    SDL_Rect ok{w - bw - 10, by, bw, 18};
    SDL_SetRenderDrawColor(ren, 44, 78, 58, 255);
    SDL_RenderFillRect(ren, &ok);
    SDL_SetRenderDrawColor(ren, 90, 200, 130, 255);
    SDL_RenderDrawRect(ren, &ok);
    texto(ok.x + 8, ok.y + 1, "Aceptar", SDL_Color{190, 240, 205, 255});
    zonas_.push_back(Zona{ok.x, ok.y, ok.w, ok.h, "dialogo_ok", ""});

    SDL_Rect no{ok.x - bw - 8, by, bw, 18};
    SDL_SetRenderDrawColor(ren, 60, 44, 48, 255);
    SDL_RenderFillRect(ren, &no);
    SDL_SetRenderDrawColor(ren, 200, 110, 120, 255);
    SDL_RenderDrawRect(ren, &no);
    texto(no.x + 8, no.y + 1, "Cancelar", SDL_Color{240, 205, 210, 255});
    zonas_.push_back(Zona{no.x, no.y, no.w, no.h, "dialogo_cancelar", ""});
  }
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
  if (now && !alt_enter_was) siguiente_vista();
  alt_enter_was = now;
#endif
}

void EmuWindow::present(const uint32_t* rgb, uint8_t leds) {
#ifdef HAVE_SDL2
  if (!active_) return;
  SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer_);
  SDL_Texture* tex = static_cast<SDL_Texture*>(texture_);

  SDL_UpdateTexture(tex, nullptr, rgb, w_ * (int)sizeof(uint32_t));

  // La caché del panel se regenera ANTES de empezar el fotograma: cambiar el
  // render target a mitad de dibujo obliga al backend (Direct3D) a descartar lo
  // que ya estaba pintado en el búfer actual y el panel salía en negro.
  void* tex_panel_top = nullptr;
  if (panel_w_ > 0 && panel_visible()) tex_panel_top = panel_cache(0, 0, leds);

  // Fondo general del panel (gris oscuro tipo escritorio).
  SDL_SetRenderDrawColor(ren, 28, 30, 34, 255);
  SDL_RenderClear(ren);

  // --- Vídeo (columna derecha) ---
  //
  // Ocupa TODO el espacio libre a la derecha del panel: se escala al mayor
  // factor entero que quepa (píxeles cuadrados, como en el FPGA) y se centra
  // en lo que sobra. Solo se estira sin mantener el factor entero cuando la
  // ventana es tan pequeña que ni ×1 cabe. Nunca se deja "a su escala base":
  // si hay sitio, se usa.
  int out_w = 0, out_h = 0;
  SDL_GetRendererOutputSize(ren, &out_w, &out_h);

  SDL_Rect vdst;
  {
    // Sin margen entre el panel y el vídeo: el separador del panel ya marca
    // la frontera y así el vídeo gana esos píxeles. En los modos sin panel, el
    // vídeo dispone de toda la ventana.
    const int ancho_panel = panel_visible() ? panel_w_ : 0;
    const int available_w = out_w - ancho_panel;
    const int available_h = out_h;
    if (available_w <= 0 || available_h <= 0) return;

    int f = available_w / w_;      // factor entero por ancho
    if (f * h_ > available_h) f = available_h / h_;  // y por alto

    if (f < 1) {
      // No cabe ni a tamaño real: se ajusta manteniendo la proporción 4:3.
      const double fx = (double)available_w / w_;
      const double fy = (double)available_h / h_;
      const double fmin = fx < fy ? fx : fy;
      const int vw = (int)(w_ * fmin);
      const int vh = (int)(h_ * fmin);
      vdst = SDL_Rect{ancho_panel + (available_w - vw) / 2, (available_h - vh) / 2,
                      vw, vh};
    } else {
      const int vw = w_ * f;
      const int vh = h_ * f;
      vdst = SDL_Rect{ancho_panel + (available_w - vw) / 2, (available_h - vh) / 2,
                      vw, vh};
    }
  }
  SDL_RenderCopy(ren, tex, nullptr, &vdst);

  // --- Botón de cambio de vista ---
  //
  // Se coloca en el margen del vídeo (pegado a su esquina superior izquierda,
  // justo fuera del framebuffer), no encima de los píxeles. Si no cabe en el
  // margen de la izquierda, va en el derecho.
  {
    const int bw = 30, bh = 26, sep = 4;
    int bx = vdst.x - bw - sep;
    if (bx < vdst.x - bw - sep || bx < 2) bx = vdst.x + vdst.w + sep;
    // En SoloVideo / PantallaCompleta el vídeo puede tocar el borde: se pone
    // dentro de la esquina, pero sobre el margen si lo hay.
    if (bx + bw > out_w - 2) bx = out_w - bw - 6;
    int by = vdst.y + 6;
    if (by + bh > out_h - 2) by = out_h - bh - 6;
    if (by < 2) by = 2;
    dibujar_boton_vista(ren, bx, by, bw, bh, vista_ != Vista::PantallaCompleta);
  }

  // Alto útil del panel: el de la ventana (o el de la pantalla a fullscreen).
  const int area_h = out_h > 0 ? out_h : h_ * scale_;

  // --- Panel lateral izquierdo ---
  //
  // Se organiza en módulos apilados de arriba abajo: cada uno es un bloque
  // con su título y su propio contenido. Para añadir uno nuevo basta con
  // calcular su rectángulo a partir de `y` y restar su alto, que es lo que se
  // hace aquí con la placa de LEDs y el terminal.
  if (panel_w_ > 0 && panel_visible()) {
    SDL_Rect panel{0, 0, panel_w_, area_h};
    SDL_SetRenderDrawColor(ren, 34, 37, 43, 255);
    SDL_RenderFillRect(ren, &panel);

    // Separador vertical entre el panel y el vídeo.
    SDL_SetRenderDrawColor(ren, 52, 57, 66, 255);
    SDL_RenderDrawLine(ren, panel_w_ - 1, 0, panel_w_ - 1, area_h);

    // Placa + LEDs + puerto 1 van cacheados en una textura: son cientos de
    // llamadas SDL que solo cambian cuando cambia el hardware. La consola va
    // aparte, en su propia textura.
    const int alto_estado = ALTO_ESTADO_PANEL;
    if (tex_panel_top) {
      SDL_Rect dst{0, 0, panel_w_, alto_estado};
      SDL_RenderCopy(ren, static_cast<SDL_Texture*>(tex_panel_top), nullptr, &dst);
    }

    // Módulo SD, entre el puerto 1 y la consola. Su alto depende del número de
    // archivos, así que se dibuja antes de colocarla.
    // Las zonas clicables se regeneran aquí: `present()` es el único sitio que
    // las rellena y `clic_en()` las consume en la misma vuelta.
    zonas_.clear();
    const int alto_sd = dibujar_modulo_sd(ren, PANEL_MARGEN, alto_estado);
    const int y_consola = alto_estado + alto_sd;

    // --- Módulo 3: terminal del UART (80x24, ANSI) ---
    if (terminal_) {
      const int term_x = PANEL_MARGEN;
      const int term_w = panel_w_ - 2 * PANEL_MARGEN;
      const int term_h = TerminalState::ROWS * TERM_CH + 2 * TERM_PAD;  // 80x24 fijo
      const int term_y = y_consola;
      SDL_Rect term{term_x, term_y, term_w, term_h};
      SDL_SetRenderDrawColor(ren, 8, 10, 12, 255);
      SDL_RenderFillRect(ren, &term);
      SDL_SetRenderDrawColor(ren, 30, 48, 38, 255);
      SDL_RenderDrawRect(ren, &term);

      dibujar_terminal(ren, &term);

      // Selector de codificacion, en la cabecera del modulo (fuera de la
      // rejilla: el terminal se repinta entero y taparia cualquier cosa que se
      // dibuje encima).
      //
      // CP437 y UTF-8 son indistinguibles byte a byte (0xDB 0xB3 es "█│" en
      // CP437 y tambien una secuencia UTF-8 valida), asi que no se puede
      // adivinar: lo elige el usuario y se aplica en caliente.
      {
        const bool u8 = term_.utf8_activo();
        const char* etiqueta = u8 ? "UTF-8" : "CP437";

        const int ancho = 5 * TERM_CW + 10;
        SDL_Rect b{term.x + term.w - ancho - 4, term.y + 1, ancho, 13};
        SDL_SetRenderDrawColor(ren, 30, 34, 44, 255);
        SDL_RenderFillRect(ren, &b);
        SDL_SetRenderDrawColor(ren, u8 ? 90 : 70, u8 ? 160 : 90, u8 ? 120 : 110, 255);
        SDL_RenderDrawRect(ren, &b);
        SDL_SetRenderDrawColor(ren, u8 ? 150 : 150, u8 ? 220 : 170, u8 ? 170 : 200, 255);
        for (int i = 0; i < 5; i++) {
          draw_glyph(ren, b.x + 5 + i * TERM_CW, b.y + 2, etiqueta[i], TERM_SCALE);
        }
        zonas_.push_back(Zona{b.x, b.y, b.w, b.h, "codificacion", ""});
      }
    }
  }

  // El diálogo de texto, si está activo, se pinta al final (encima de todo).
  if (panel_w_ > 0 && panel_visible()) dibujar_dialogo(ren);
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

      // --- Ratón: módulo SD, diálogo y botón de cambio de vista ---
      case SDL_MOUSEBUTTONDOWN: {
        // Las zonas se guardan en píxeles de VENTANA (el área cliente), que es
        // el espacio en el que las dibuja el renderer. Las coordenadas del
        // evento ya vienen en ese espacio: NO hay que reescalarlas (con DPI
        // 125% el búfer de salida es mayor que la ventana y el clic acababa
        // cayendo en otra fila).
        if (ev.button.button == SDL_BUTTON_LEFT) {
          clic_en(ev.button.x, ev.button.y);
        } else if (ev.button.button == SDL_BUTTON_RIGHT) {
          // Botón derecho: vuelve a la vista normal desde cualquier modo.
          set_vista(Vista::Normal);
        }
        break;
      }

      // --- Teclado ---
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        const bool down = (ev.type == SDL_KEYDOWN);
        uint8_t& t = in.teclado;

        // Diálogo de texto abierto: el teclado es para el campo, no para el
        // joystick. Enter acepta, Esc cancela, Retroceso borra.
        if (dialogo_.activo) {
          if (!down) break;
          // Al primer carácter se descarta el nombre viejo: el campo arranca
          // seleccionado para no tener que borrarlo a mano.
          auto escribir = [&](char c) {
            if (dialogo_.seleccionado) {
              dialogo_.texto.clear();
              dialogo_.seleccionado = false;
            }
            if (dialogo_.texto.size() < 12) dialogo_.texto.push_back(c);
          };
          if (ev.key.keysym.sym == SDLK_RETURN ||
              ev.key.keysym.sym == SDLK_KP_ENTER) {
            dialogo_.activo = false;
            if (on_sd_accion) {
              on_sd_accion(dialogo_.accion + "_ok",
                           dialogo_.arg + '\n' + dialogo_.texto);
            }
          } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
            dialogo_.activo = false;
          } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
            if (dialogo_.seleccionado) {
              dialogo_.texto.clear();
              dialogo_.seleccionado = false;
            } else if (!dialogo_.texto.empty()) {
              dialogo_.texto.pop_back();
            }
          } else if (ev.key.keysym.sym >= 32 && ev.key.keysym.sym < 127) {
            char c = (char)ev.key.keysym.sym;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            escribir(c);
          }
          break;
        }
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
            if (down) siguiente_vista();
            break;
          // Esc: vuelve a la vista normal desde los modos de solo vídeo.
          // (No se usa para el juego: los juegos leen el joystick, no el
          // teclado del terminal, que es de solo lectura.)
          case SDLK_ESCAPE:
            if (down && vista_ != Vista::Normal) set_vista(Vista::Normal);
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
