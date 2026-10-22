// Pruebas del emulador de terminal (80x24, ANSI + CP437).
//
// Compilar y ejecutar:
//   make test
//
// Además de los casos sintéticos, reproduce la salida de un binario real si se
// le pasa una ruta por argumento (útil con el Tetris de consola y con EDITOR.BIN):
//
//   ./test_terminal D:/Proyectos/tetris_console/tetris.bin
//
// Nota: el .bin no es solo texto; contiene código de máquina. Al volcarlo byte a
// byte se interpreta como si la CPU lo hubiera transmitido entero, que es lo que
// permite ver aquí si el intérprete entiende las secuencias que usa.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/terminal/terminal_state.h"

static int fallos = 0;

static void comprobar(const char* nombre, const std::string& obtenido,
                      const std::string& esperado) {
  if (obtenido == esperado) {
    std::printf("  OK    %s\n", nombre);
  } else {
    std::printf("  FALLO %s\n    esperado: [%s]\n    obtenido: [%s]\n", nombre,
                esperado.c_str(), obtenido.c_str());
    fallos++;
  }
}

static void comprobar_bool(const char* nombre, bool cond) {
  if (cond) {
    std::printf("  OK    %s\n", nombre);
  } else {
    std::printf("  FALLO %s\n", nombre);
    fallos++;
  }
}

static void enviar(TerminalState& t, const std::string& s) {
  for (unsigned char c : s) t.feed(c);
}

// Texto de una fila, sin los espacios de la derecha.
static std::string fila(const TerminalState& t, int f) {
  std::string out;
  for (int c = 0; c < TerminalState::COLS; c++) out.push_back((char)t.celda(f, c).ch);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

int main(int argc, char** argv) {
  // 1. Texto simple y CRLF.
  {
    TerminalState t;
    enviar(t, "Hola\r\nMundo\r\n");
    comprobar("CRLF fila 0", fila(t, 0), "Hola");
    comprobar("CRLF fila 1", fila(t, 1), "Mundo");
    comprobar_bool("CRLF: cursor en fila 2", t.cursor_fila() == 2);
  }

  // 2. Borrar pantalla y posicionar.
  {
    TerminalState t;
    enviar(t, "basura por todas partes\r\nmas basura");
    enviar(t, "\x1b[2J\x1b[H");
    comprobar("ED2H limpia", fila(t, 0) + fila(t, 1), "");
    enviar(t, "\x1b[5;10H");
    comprobar_bool("CUP 5;10 -> fila 4 col 9",
                   t.cursor_fila() == 4 && t.cursor_col() == 9);
    enviar(t, "X");
    comprobar_bool("escribe en 4,9", t.celda(4, 9).ch == 'X');
  }

  // 3. EL (borrar línea) y ECH.
  {
    TerminalState t;
    enviar(t, "1234567890\x1b[1;5H\x1b[K");
    comprobar("EL0 borra desde el cursor", fila(t, 0), "1234");
    enviar(t, "\x1b[1;1H\x1b[2X");
    comprobar("ECH borra 2 caracteres", fila(t, 0), "  34");
  }

  // 4. SGR: colores y negrita.
  {
    TerminalState t;
    enviar(t, "\x1b[31mA\x1b[0mB");
    comprobar_bool("SGR 31 = rojo", t.celda(0, 0).at.fg == 1);
    comprobar_bool("SGR 0 = reset", t.celda(0, 1).at.fg == 7);
    enviar(t, "\x1b[31;1mC");
    comprobar_bool("SGR 31;1 fg 1 + negrita",
                   t.celda(0, 2).at.fg == 1 && t.celda(0, 2).at.negrita);
    enviar(t, "\x1b[7mD");
    comprobar_bool("SGR 7 = inverso", t.celda(0, 3).at.inverso);
  }

  // 5. Cursor visible/oculto.
  {
    TerminalState t;
    enviar(t, "\x1b[?25l");
    comprobar_bool("?25l oculta cursor", !t.cursor_visible());
    enviar(t, "\x1b[?25h");
    comprobar_bool("?25h muestra cursor", t.cursor_visible());
  }

  // 6. Scroll: 30 líneas en una pantalla de 24.
  {
    TerminalState t;
    for (int i = 0; i < 30; i++) {
      enviar(t, "L" + std::to_string(i) + "\r\n");
    }
    // 30 líneas de texto + el LF final: el contenido ocupa 30 filas y el
    // cursor queda en la 30, así que salen 7 líneas por arriba.
    comprobar_bool("scroll: 7 líneas al historial", t.historial().size() == 7);
    comprobar_bool("scroll: última línea con texto es L29", fila(t, 22) == "L29");
    comprobar_bool("scroll: historial conserva L0",
                   t.historial().front()[0].ch == 'L' &&
                       t.historial().front()[1].ch == '0');
  }

  // 7. CP437: los bytes altos se guardan tal cual (el dibujo los pinta).
  {
    TerminalState t;
    enviar(t, std::string("\xDB\xB3\xDA"));
    // 0xDB 0xB3 0xDA es tambien una secuencia UTF-8 valida (U+06F3), asi que
    // se decodifica en UNA celda. Lo que importa para /console.txt es que el
    // byte CP437 guardado sea estable entre modos.
    comprobar_bool("secuencia ambigua: una celda", t.celda(0, 1).ch == 0x20);
    comprobar_bool("secuencia ambigua: ch CP437 valido", t.celda(0, 0).ch == 0xDB);
  }

  // 7b. Bytes CP437 sueltos (no forman UTF-8): se conservan uno a uno.
  {
    TerminalState t;
    // Un byte 0xDA al final se queda esperando continuacion (no se puede saber
    // si es CP437 o una secuencia cortada); se cierra con un espacio.
    enviar(t, std::string("\xDB\x41\xDA "));
    comprobar_bool("CP437 suelto 0xDB", t.celda(0, 0).ch == 0xDB);
    comprobar_bool("CP437 ASCII", t.celda(0, 1).ch == 'A');
    comprobar_bool("CP437 suelto 0xDA", t.celda(0, 2).ch == 0xDA);
  }

  // 7c. a_texto() da el mismo texto en los dos modos (un byte por celda).
  {
    TerminalState a, b;
    b.set_utf8(true);
    const std::string s = std::string("Hola \xE2\x99\x9C fin");
    enviar(a, s);
    enviar(b, s);
    comprobar_bool("/console.txt igual en CP437 y UTF-8", a.a_texto() == b.a_texto());
  }

  // 8. UTF-8: una secuencia multibyte ocupa UNA celda (no una por byte).
  {
    TerminalState t;
    t.set_utf8(true);
    // ♞ (U+265E) = E2 99 9E, seguido de 'X'.
    enviar(t, std::string("\xE2\x99\x9E" "X"));
    comprobar_bool("UTF-8: ficha es una celda", t.celda(0, 1).ch == 'X');
    comprobar_bool("UTF-8: punto de codigo guardado", t.celda(0, 0).cp == 0x265E);
    // Los acentos UTF-8 se traducen a su byte CP437.
    TerminalState t2;
    t2.set_utf8(true);
    enviar(t2, std::string("\xC3\xA1"));  // á
    comprobar_bool("UTF-8: acento a CP437", t2.celda(0, 0).ch == 0xA0);
    // Y con UTF-8 apagado, los mismos bytes son CP437 crudo.
    TerminalState t3;
    enviar(t3, std::string("\xE2\x99\x9E"));
    comprobar_bool("UTF-8 apagado: bytes CP437", t3.celda(0, 0).ch == 0xE2);
  }

  // 9. Secuencia desconocida: no debe ensuciar la pantalla.
  {
    TerminalState t;
    enviar(t, "\x1b[99;99zHola");
    comprobar("CSI desconocida se ignora", fila(t, 0), "Hola");
    enviar(t, "\x1b]0;titulo\x07OK");
    comprobar("OSC se ignora", fila(t, 0), "HolaOK");
  }

  // 9. TAB y backspace.
  {
    TerminalState t;
    enviar(t, "a\tb");
    comprobar_bool("TAB: b en la columna 8", t.celda(0, 8).ch == 'b');
    comprobar_bool("TAB: cursor en la columna 9", t.cursor_col() == 9);
    enviar(t, "\x08X");
    // BS retrocede de la 9 a la 8 y X sobrescribe la b.
    comprobar_bool("BS retrocede y sobrescribe", t.celda(0, 8).ch == 'X');
  }

  // 10. Auto-wrap al llegar a la columna 80.
  {
    TerminalState t;
    enviar(t, std::string(80, 'A') + "B");
    comprobar_bool("auto-wrap: 80 A en fila 0", fila(t, 0) == std::string(80, 'A'));
    comprobar_bool("auto-wrap: B en fila 1", t.celda(1, 0).ch == 'B');
  }

  // --- Prueba con un binario real (opcional) ---
  if (argc > 1) {
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) {
      std::printf("  AVISO no se pudo abrir %s\n", argv[1]);
    } else {
      std::vector<char> datos((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
      TerminalState t;
      for (unsigned char c : datos) t.feed(c);
      std::printf("\n--- salida interpretada de %s (%zu bytes) ---\n", argv[1],
                  datos.size());
      std::printf("cursor en %d,%d | scrollback: %zu lineas\n", t.cursor_fila(),
                  t.cursor_col(), t.historial().size());
      // Se muestran las últimas 12 filas no vacías.
      int mostradas = 0;
      for (int i = TerminalState::ROWS - 1; i >= 0 && mostradas < 12; i--) {
        std::string l = fila(t, i);
        if (!l.empty()) {
          std::printf("|%s|\n", l.c_str());
          mostradas++;
        }
      }
      // Qué secuencias se han visto: si hay marcos CP437 en pantalla, se ven
      // caracteres '#' en este pequeño resumen.
      int altos = 0;
      for (int i = 0; i < TerminalState::ROWS; i++)
        for (int c = 0; c < TerminalState::COLS; c++)
          if (t.celda(i, c).ch >= 0x80) altos++;
      std::printf("celdas con byte >= 0x80: %d\n", altos);
    }
  }

  std::printf(fallos ? "\n%d FALLO(S)\n" : "\nTodo correcto\n", fallos);
  return fallos ? 1 : 0;
}
