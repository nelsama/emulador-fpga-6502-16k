#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// Estado de un terminal de texto de 80x24, alimentado por bytes del UART.
//
// Es SOLO LECTURA respecto a la emulación: interpreta lo que la CPU transmite
// para poder mostrarlo como lo vería un usuario con PuTTY. Nunca escribe nada
// hacia atrás.
//
// Qué interpreta:
//   - Control: CR, LF, BS, TAB, BEL (ignorado).
//   - Cursor:  CUP/H (fila;col), CUU/CUD/CUF/CUB (A/B/C/D), CHA/G (columna),
//              CNL/CPL (E/F), HVP/f.
//   - Borrado: ED/J (0,1,2), EL/K (0,1,2), ECH/X.
//   - SGR/m:   0 (reset), 1 (negrita), 2 (atenuado), 7 (inverso), 22, 27,
//              30-37, 39, 40-47, 49, 90-97, 100-107, 38;5;n, 48;5;n.
//   - Modo:    ?25 l/h (ocultar/mostrar cursor) — el resto se ignora.
//   - Otros ESC: se descarta la secuencia sin efecto.
//
// Los bytes 0x80-0xFF se almacenan tal cual y se pintan con la tabla CP437 (la
// misma que usa la consola de DOS y PuTTY en modo DOS). No hay UTF-8: el UART
// mueve bytes sueltos.
//
// Geometría fija 80x24 (lo que asumen los programas del sistema). Las líneas
// que salen por arriba se guardan en un historial para poder consultarlas.
class TerminalState {
public:
  static constexpr int COLS = 80;
  static constexpr int ROWS = 24;

  // Atributos de una celda (paleta ANSI de 16 colores).
  struct Atributos {
    uint8_t fg = 7;      // color de texto (0-15), 7 = gris claro
    uint8_t bg = 0;      // color de fondo, 0 = negro
    bool negrita = false;
    bool inverso = false;
  };

  struct Celda {
    uint8_t ch = 0x20;   // byte CP437 (0x20 = espacio)
    // Punto de codigo Unicode cuando el texto llego en UTF-8. 0 = sin dato y
    // se usa `ch` con la tabla CP437. Asi el dibujo no depende de la
    // aplicacion: solo de lo que el programa escribio en el cable.
    uint32_t cp = 0;
    Atributos at;
  };

  TerminalState() { limpiar(); }

  // Procesa un byte del flujo de transmisión.
  void feed(uint8_t b);

  // Borra todo (pantalla, historial y atributos).
  void limpiar();

  // Acceso a la rejilla visible.
  const Celda& celda(int fila, int col) const {
    return rejilla_[fila * COLS + col];
  }

  int cursor_fila() const { return fila_; }
  int cursor_col() const { return col_; }
  bool cursor_visible() const { return cursor_visible_; }
  bool sucio() const { return sucio_; }
  void limpiar_sucio() { sucio_ = false; }

  // Líneas que han salido por arriba, de la más antigua a la más reciente.
  // Se guardan solo los bytes y los atributos de cada celda.
  const std::deque<std::vector<Celda>>& historial() const { return historial_; }
  static constexpr size_t HISTORIAL_MAX = 1000;

  // Texto plano de la pantalla (80x24), útil para /console.txt y los tests.
  // Rejilla como texto, SIEMPRE en CP437: un byte por celda, sin convertir.
  // Es el formato que exporta /console.txt, independiente del modo que tenga
  // el panel. Si la celda vino de UTF-8, se devuelve el byte CP437 a que se
  // traduce, de modo que la salida quede siempre igual para una misma pantalla.
  std::string a_texto() const;

  // Pinta la pantalla a un búfer de píxeles 0x00RRGGBB (8x16 por celda), con
  // los colores ANSI. Lo usa /console.png para dar una captura de la consola.
  void a_imagen(std::vector<uint32_t>* px, int* w, int* h) const;

  // Glifo de una celda: 16 bytes, una fila por byte, bit 7 = pixel izquierdo.
  // Prefiere la fuente Unicode si la celda trae punto de codigo; si no, CP437.
  // Lo usan tanto a_imagen() como el panel de la ventana.
  static const uint8_t* glifo(const Celda& c, bool utf8);

  // --- Codificacion de entrada ---
  //
  // Los bytes no dicen si son CP437 o UTF-8: 0xDB 0xB3 es "█│" en CP437 y
  // tambien una secuencia UTF-8 valida. No se puede adivinar, asi que lo elige
  // el usuario desde el panel y se aplica en caliente.
  //
  // Con UTF-8 desactivado (por defecto) todo byte alto se pinta como CP437,
  // que es lo que hace el sistema clasico. Al activarlo, las secuencias UTF-8
  // ocupan una sola celda y se busca el glifo en font_unicode.
  void set_utf8(bool on) {
    utf8_activo_ = on;
    utf8_necesarios_ = 0;
    utf8_cp_ = 0;
    sucio_ = true;
  }
  bool utf8_activo() const { return utf8_activo_; }
  // Glifo de una celda segun el modo.
  const uint8_t* glifo_modo(const Celda& c) const { return glifo(c, utf8_activo_); }

private:
  void escribir(uint8_t ch);
  // Igual, pero guarda ademas el punto de codigo Unicode de origen.
  void escribir(uint8_t ch, uint32_t cp);
  // Traduce un punto de codigo Unicode a una celda CP437 (una sola).
  static uint8_t unicode_a_cp437(uint32_t cp);

  // Decodificador UTF-8: los programas nuevos mandan texto en UTF-8 y el panel
  // dibuja CP437, asi que una secuencia multibyte debe ocupar UNA celda.
  int utf8_necesarios_ = 0;  // bytes de continuacion que faltan
  uint32_t utf8_cp_ = 0;     // punto de codigo en construccion
  uint8_t utf8_byte_ = 0;    // byte original de la cabecera (si falla, CP437)
  bool utf8_activo_ = false; // CP437 por defecto; el panel lo conmuta
  void nueva_linea();
  void scroll();
  void mover(int dfila, int dcol);
  void ejecutar_escape(char cmd);
  void aplicar_sgr(const std::vector<int>& params);
  void limpiar_pantalla(int modo);
  void limpiar_linea(int modo);
  void borrar_caracteres(int n);
  void siguiente_columna_tab();
  void reset_atributos();

  std::vector<Celda> rejilla_;
  std::deque<std::vector<Celda>> historial_;

  int fila_ = 0;
  int col_ = 0;
  Atributos at_;
  bool cursor_visible_ = true;
  bool sucio_ = false;

  // Estado de la secuencia de escape en curso.
  enum class Esc { Ninguna, Escape, Csi, CsiIntermedio };
  Esc esc_ = Esc::Ninguna;
  std::string params_;        // dígitos y ';' de la CSI
  std::string intermedio_;    // '?' y similares
  bool ultimo_fue_cr_ = false; // LF tras CR no vuelve a bajar (CRLF llega partido)
};
