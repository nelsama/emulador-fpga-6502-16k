#include "terminal_state.h"

#include <cstdio>

#include "font_cp437.h"
#include "font_unicode.h"

// Intérprete de terminal para el panel de depuración. Ver terminal_state.h para
// el alcance y el porqué de cada decisión.

void TerminalState::limpiar() {
  rejilla_.assign((size_t)COLS * ROWS, Celda{});
  historial_.clear();
  fila_ = 0;
  col_ = 0;
  reset_atributos();
  cursor_visible_ = true;
  sucio_ = true;
  esc_ = Esc::Ninguna;
  params_.clear();
  intermedio_.clear();
  ultimo_fue_cr_ = false;
}

void TerminalState::reset_atributos() { at_ = Atributos{}; }

void TerminalState::feed(uint8_t b) {
  // --- Secuencia de escape en curso ---
  if (esc_ == Esc::Escape) {
    if (b == '[') {
      esc_ = Esc::Csi;
      params_.clear();
      intermedio_.clear();
      return;
    }
    if (b == ']' || b == 'P' || b == '^' || b == '_') {
      // OSC/DCS/PM/APC: se consumen hasta BEL o ST (ESC \). No los usa este
      // sistema, pero si llegan no deben ensuciar la pantalla.
      esc_ = Esc::CsiIntermedio; // se reutiliza: "ignorar hasta terminador"
      params_.assign("\x01osc");
      return;
    }
    if (b == '(' || b == ')' || b == '#') {
      esc_ = Esc::CsiIntermedio; // selector de juego de caracteres: se ignora
      params_.assign("\x01skip1");
      return;
    }
    // ESC <otra cosa>: se ignora el ESC y se procesa el byte normalmente.
    esc_ = Esc::Ninguna;
    // cae al procesado normal
  } else if (esc_ == Esc::Csi) {
    if (b >= '0' && b <= '9') { params_.push_back((char)b); return; }
    if (b == ';') { params_.push_back(';'); return; }
    if (b == '?' || b == '>' || b == '!' || b == '=') { intermedio_.push_back((char)b); return; }
    if (b >= 0x20 && b <= 0x2F) { intermedio_.push_back((char)b); return; }
    if (b >= 0x40 && b <= 0x7E) {
      ejecutar_escape((char)b);
      esc_ = Esc::Ninguna;
      return;
    }
    // Byte inesperado: se descarta la secuencia.
    esc_ = Esc::Ninguna;
    return;
  } else if (esc_ == Esc::CsiIntermedio) {
    // Ignorando el resto de una secuencia que no nos interesa.
    if (params_ == "\x01skip1") { esc_ = Esc::Ninguna; return; }
    if (b == 0x07 || b == '\\') esc_ = Esc::Ninguna;  // BEL o ST
    return;
  }

  // --- Caracteres normales ---
  switch (b) {
    case 0x1B:  // ESC
      esc_ = Esc::Escape;
      return;
    case '\r':
      col_ = 0;
      ultimo_fue_cr_ = true;
      sucio_ = true;
      return;
    case '\n':
      // Baja de línea y deja el cursor en la columna 0.
      //
      // El monitor manda "\r\n", pero un LF suelto también debe empezar
      // línea (es lo que hace un terminal real con LF sin CR). Que venga un CR
      // justo antes no cambia nada: ir a columna 0 y bajar es lo mismo que
      // hacerlo en un solo paso.
      col_ = 0;
      nueva_linea();
      ultimo_fue_cr_ = false;
      return;
    case 0x08:  // BS
      if (col_ > 0) col_--;
      ultimo_fue_cr_ = false;
      sucio_ = true;
      return;
    case '\t':
      siguiente_columna_tab();
      ultimo_fue_cr_ = false;
      return;
    case 0x07:  // BEL: se ignora (no hay pitido en el panel)
      return;
    default:
      break;
  }

  if (b < 0x20 || b == 0x7F) return;  // otros controles: sin efecto

  // --- UTF-8 ---
  //
  // El sistema es CP437 y el panel dibuja una tabla CP437 de 8x16, pero un
  // programa cualquiera puede mandar UTF-8: '♞' es E2 99 9E. Sin decodificar,
  // esos tres bytes se pintaban como tres celdas en vez de una.
  //
  // Aqui solo se decodifica el PROTOCOLO (una secuencia multibyte ocupa una
  // celda). Que glifo usar para cada caracter no es cosa del terminal: eso lo
  // decide la fuente del panel.
  //
  // El problema: los bytes 0x80-0xBF son a la vez continuacion UTF-8 y glifos
  // CP437 (0xDB = bloque, 0xB3 = barra vertical), y 0xDB 0xB3 es incluso una
  // secuencia UTF-8 valida. No se puede adivinar: se decodifica UTF-8 solo si
  // el usuario lo activa desde el panel (set_utf8).
  if (b >= 0x80 && !utf8_activo_) {
    escribir(b);
    ultimo_fue_cr_ = false;
    return;
  }

  if (b >= 0x80) {
    // Si habia una cabecera pendiente y este byte no es continuacion, la
    // cabecera era en realidad un byte CP437 suelto: se pinta y se sigue.
    if (utf8_necesarios_ > 0 && (b & 0xC0) != 0x80) {
      utf8_necesarios_ = 0;
      escribir(utf8_byte_);
    }

    if (utf8_necesarios_ == 0) {
      if (b >= 0xE0 && b <= 0xEF) {
        // Cabecera de 3 bytes (U+0800-U+FFFF).
        utf8_necesarios_ = 2;
        utf8_cp_ = (uint32_t)(b & 0x0F);
        utf8_byte_ = b;
        return;
      }
      if (b >= 0xC2 && b <= 0xDF) {
        // Cabecera de 2 bytes (U+0080-U+07FF).
        utf8_necesarios_ = 1;
        utf8_cp_ = (uint32_t)(b & 0x1F);
        utf8_byte_ = b;
        return;
      }
      // Byte alto que no inicia una secuencia valida.
      escribir(b);
      ultimo_fue_cr_ = false;
      return;
    }

    // Continuacion de una secuencia en curso.
    utf8_cp_ = (utf8_cp_ << 6) | (uint32_t)(b & 0x3F);
    if (--utf8_necesarios_ == 0) {
      escribir(unicode_a_cp437(utf8_cp_), utf8_cp_);
      utf8_cp_ = 0;
      ultimo_fue_cr_ = false;
    }
    return;
  }

  // Byte ASCII normal: si habia una cabecera pendiente, era CP437 suelto.
  if (utf8_necesarios_ > 0) {
    utf8_necesarios_ = 0;
    escribir(utf8_byte_);
  }

  // 0x20-0x7F.
  escribir(b);
  ultimo_fue_cr_ = false;
}

// Traduce un punto de codigo Unicode a la celda CP437 que lo representa.
//
// CP437 cubre latin-1, la mitad de las cajas y los bloques, asi que el texto
// normal y los marcos pasan intactos. Un caracter que CP437 no puede
// representar se sustituye por '?', de modo que siga ocupando UNA celda.
uint8_t TerminalState::unicode_a_cp437(uint32_t cp) {
  if (cp < 0x80) return (uint8_t)cp;

  // Tabla de los 128 glifos altos de CP437 en orden (Unicode).
  static const uint16_t kCp437Altos[128] = {
      0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
      0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
      0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
      0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
      0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
      0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
      0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
      0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
      0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
      0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
      0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
      0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
      0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
      0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
      0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
      0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

  for (int i = 0; i < 128; i++) {
    if (kCp437Altos[i] == cp) return (uint8_t)(0x80 + i);
  }

  // Cajas y bloques que UTF-8 trae con otro punto de codigo.
  switch (cp) {
    case 0x2022: return 0x07;  // vineta -> punto
    case 0x2013: case 0x2014: return '-';  // guiones largos
    case 0x2018: case 0x2019: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2026: return '.';   // puntos suspensivos
    case 0x00A0: return ' ';   // espacio duro
    default: break;
  }

  // Caracter no representable: un solo '?' (nunca varios por caracter).
  return '?';
}

void TerminalState::escribir(uint8_t ch) { escribir(ch, 0); }

void TerminalState::escribir(uint8_t ch, uint32_t cp) {
  Celda& c = rejilla_[(size_t)fila_ * COLS + col_];
  c.ch = ch;
  c.cp = cp;
  c.at = at_;
  sucio_ = true;
  if (++col_ >= COLS) {
    // Auto-wrap: al pasar de la última columna se baja de línea. Se deja el
    // cursor al principio de la siguiente.
    col_ = 0;
    nueva_linea();
  }
}

void TerminalState::nueva_linea() {
  if (fila_ + 1 >= ROWS) {
    scroll();
  } else {
    fila_++;
  }
  sucio_ = true;
}

void TerminalState::scroll() {
  // La primera fila pasa al historial.
  std::vector<Celda> linea(rejilla_.begin(), rejilla_.begin() + COLS);
  historial_.push_back(std::move(linea));
  if (historial_.size() > HISTORIAL_MAX) historial_.pop_front();

  // Sube todo una fila y deja la última en blanco.
  for (int f = 0; f < ROWS - 1; f++) {
    for (int c = 0; c < COLS; c++) {
      rejilla_[(size_t)f * COLS + c] = rejilla_[(size_t)(f + 1) * COLS + c];
    }
  }
  for (int c = 0; c < COLS; c++) {
    rejilla_[(size_t)(ROWS - 1) * COLS + c] = Celda{};
  }
  sucio_ = true;
}

void TerminalState::mover(int dfila, int dcol) {
  fila_ += dfila;
  col_ += dcol;
  if (fila_ < 0) fila_ = 0;
  if (fila_ >= ROWS) fila_ = ROWS - 1;
  if (col_ < 0) col_ = 0;
  if (col_ >= COLS) col_ = COLS - 1;
  sucio_ = true;
}

void TerminalState::siguiente_columna_tab() {
  col_ = ((col_ / 8) + 1) * 8;
  if (col_ >= COLS) col_ = COLS - 1;
  sucio_ = true;
}

void TerminalState::borrar_caracteres(int n) {
  for (int i = 0; i < n && col_ + i < COLS; i++) {
    Celda& c = rejilla_[(size_t)fila_ * COLS + col_ + i];
    c.ch = 0x20;
    c.at = at_;
  }
  sucio_ = true;
}

void TerminalState::limpiar_linea(int modo) {
  // 0: del cursor al final; 1: del principio al cursor; 2: toda la línea.
  int desde = 0, hasta = COLS - 1;
  if (modo == 0) desde = col_;
  else if (modo == 1) hasta = col_;
  for (int c = desde; c <= hasta; c++) {
    Celda& cel = rejilla_[(size_t)fila_ * COLS + c];
    cel.ch = 0x20;
    cel.at = at_;
  }
  sucio_ = true;
}

void TerminalState::limpiar_pantalla(int modo) {
  if (modo == 2 || modo == 3) {
    for (auto& c : rejilla_) c = Celda{};
    if (modo == 3) historial_.clear();
    fila_ = 0;
    col_ = 0;
  } else if (modo == 0) {
    // Del cursor al final.
    limpiar_linea(0);
    for (int f = fila_ + 1; f < ROWS; f++) {
      for (int c = 0; c < COLS; c++) rejilla_[(size_t)f * COLS + c] = Celda{};
    }
  } else if (modo == 1) {
    // Del principio al cursor.
    limpiar_linea(1);
    for (int f = 0; f < fila_; f++) {
      for (int c = 0; c < COLS; c++) rejilla_[(size_t)f * COLS + c] = Celda{};
    }
  }
  sucio_ = true;
}

void TerminalState::aplicar_sgr(const std::vector<int>& p) {
  if (p.empty()) {
    reset_atributos();
    return;
  }
  for (size_t i = 0; i < p.size(); i++) {
    const int v = p[i];
    if (v == 0) {
      reset_atributos();
    } else if (v == 1) {
      at_.negrita = true;
    } else if (v == 2) {
      at_.negrita = false;  // atenuado: sin efecto visible
    } else if (v == 7) {
      at_.inverso = true;
    } else if (v == 22) {
      at_.negrita = false;
    } else if (v == 27) {
      at_.inverso = false;
    } else if (v >= 30 && v <= 37) {
      at_.fg = (uint8_t)(v - 30);
    } else if (v == 39) {
      at_.fg = 7;
    } else if (v >= 40 && v <= 47) {
      at_.bg = (uint8_t)(v - 40);
    } else if (v == 49) {
      at_.bg = 0;
    } else if (v >= 90 && v <= 97) {
      at_.fg = (uint8_t)(v - 90 + 8);
    } else if (v >= 100 && v <= 107) {
      at_.bg = (uint8_t)(v - 100 + 8);
    } else if ((v == 38 || v == 48) && i + 2 < p.size() && p[i + 1] == 5) {
      // 38;5;n / 48;5;n: color de 256. Se aproxima a los 16 primeros.
      const int n = p[i + 2];
      const uint8_t aprox = (uint8_t)(n < 16 ? n : (n < 232 ? 7 : 7));
      if (v == 38) at_.fg = aprox;
      else at_.bg = aprox;
      i += 2;
    }
  }
}

void TerminalState::ejecutar_escape(char cmd) {
  // Parámetros: "" -> un valor por defecto; "5;3" -> {5,3}.
  std::vector<int> p;
  if (!params_.empty()) {
    int v = 0;
    bool hay = false;
    for (char ch : params_) {
      if (ch == ';') {
        p.push_back(hay ? v : 0);
        v = 0;
        hay = false;
      } else {
        v = v * 10 + (ch - '0');
        hay = true;
      }
    }
    p.push_back(hay ? v : 0);
  }
  auto param = [&](size_t i, int def) { return i < p.size() && p[i] > 0 ? p[i] : def; };

  switch (cmd) {
    case 'A': mover(-param(0, 1), 0); break;                    // CUU
    case 'B': mover(param(0, 1), 0); break;                     // CUD (y CNL sin cambio)
    case 'C': mover(0, param(0, 1)); break;                     // CUF
    case 'D': mover(0, -param(0, 1)); break;                    // CUB
    case 'E': {                                                  // CNL
      const int n = param(0, 1);
      mover(n, 0);
      col_ = 0;
      break;
    }
    case 'F': {                                                  // CPL
      const int n = param(0, 1);
      mover(-n, 0);
      col_ = 0;
      break;
    }
    case 'G':                                                    // CHA
      col_ = param(0, 1) - 1;
      if (col_ < 0) col_ = 0;
      if (col_ >= COLS) col_ = COLS - 1;
      sucio_ = true;
      break;
    case 'H':
    case 'f': {                                                  // CUP
      fila_ = param(0, 1) - 1;
      col_ = param(1, 1) - 1;
      if (fila_ < 0) fila_ = 0;
      if (fila_ >= ROWS) fila_ = ROWS - 1;
      if (col_ < 0) col_ = 0;
      if (col_ >= COLS) col_ = COLS - 1;
      sucio_ = true;
      break;
    }
    case 'J': limpiar_pantalla(p.empty() ? 0 : p[0]); break;     // ED
    case 'K': limpiar_linea(p.empty() ? 0 : p[0]); break;        // EL
    case 'X': borrar_caracteres(param(0, 1)); break;             // ECH
    case 'm': aplicar_sgr(p); break;                             // SGR
    case 'h':
    case 'l':
      // ?25h / ?25l: cursor visible / oculto. Otros modos se ignoran.
      if (!intermedio_.empty() && intermedio_[0] == '?' && !p.empty() && p[0] == 25) {
        cursor_visible_ = (cmd == 'h');
        sucio_ = true;
      }
      break;
    default:
      break;  // resto: sin efecto
  }
}

std::string TerminalState::a_texto() const {
  std::string out;
  out.reserve((size_t)(COLS + 1) * ROWS);
  for (int f = 0; f < ROWS; f++) {
    for (int c = 0; c < COLS; c++) {
      const uint8_t ch = rejilla_[(size_t)f * COLS + c].ch;
      out.push_back((char)(ch < 0x20 ? 0x20 : ch));
    }
    // Sin relleno a la derecha: 80 columnas fijas para comparar con PuTTY.
    out.push_back('\n');
  }
  return out;
}

namespace {
// Paleta ANSI de 16 colores (la de consola VGA), igual que en la ventana.
const uint32_t kPaleta[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};
}  // namespace

const uint8_t* TerminalState::glifo(const Celda& c) {
  // Si la celda vino de UTF-8, se busca su punto de codigo en la fuente
  // Unicode. Si no esta (rango no incluido), se cae a CP437 con el byte que
  // se guardo; si tampoco hay nada, un '?' de una sola celda.
  if (c.cp != 0) {
    for (int i = 0; i < font_unicode::kNumIndices; i++) {
      if (font_unicode::kIndices[i].cp == c.cp) {
        return &font_unicode::kGlifos[font_unicode::kIndices[i].offset];
      }
    }
  }
  return &kFontCp437[(size_t)c.ch * 16];
}

void TerminalState::a_imagen(std::vector<uint32_t>* px, int* w, int* h) const {
  const int ancho = COLS * 8;
  const int alto = ROWS * 16;
  px->assign((size_t)ancho * alto, 0x000000);
  *w = ancho;
  *h = alto;
  for (int f = 0; f < ROWS; f++) {
    for (int c = 0; c < COLS; c++) {
      const Celda& cel = rejilla_[(size_t)f * COLS + c];
      int fg = cel.at.fg & 15;
      int bg = cel.at.bg & 15;
      if (cel.at.negrita) fg |= 8;
      if (cel.at.inverso) {
        const int t = fg;
        fg = bg;
        bg = t;
      }
      const uint32_t col_fg = kPaleta[fg];
      const uint32_t col_bg = kPaleta[bg];
      const uint8_t* g = glifo(cel);
      uint32_t* fila = px->data() + (size_t)f * 16 * ancho + (size_t)c * 8;
      for (int y = 0; y < 16; y++) {
        const uint8_t bits = g[y];
        for (int x = 0; x < 8; x++) {
          fila[(size_t)y * ancho + x] = (bits & (0x80 >> x)) ? col_fg : col_bg;
        }
      }
    }
  }
}
