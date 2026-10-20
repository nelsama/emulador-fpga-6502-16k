// Prueba del tap del UART: comprueba que el texto del terminal se construye
// con la misma semántica que una consola tonta (CR, LF, BS, ESC, bytes >0x7F).
//
// Compilar y ejecutar (MSYS2):
//   g++ -std=c++17 -O2 -Wall -Isrc -o test_uart_tap tools/test_uart_tap.cpp src/uart.cpp
//   ./test_uart_tap
#include <cstdio>
#include <string>

#include "../src/uart.h"

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

// Escribe byte a byte por el registro TX, como hace la CPU.
// Ojo: se recorre como `unsigned char`; con `char` con signo, bytes altos como
// 0xE1 llegan como negativos al escribir por el cable (que sí es uint8_t).
static void enviar(Uart& u, const std::string& s) {
  for (unsigned char c : s) {
    bool resp = false;
    u.write(0xC020, (uint8_t)c, resp);
  }
}

int main() {
  Uart u;

  // 1. CRLF normal: cada '\r\n' abre una línea y no se pierde texto.
  enviar(u, "Hola\r\nMundo\r\n");
  comprobar("CRLF", u.tap_text(), "Hola\nMundo\n");

  // 2. Borrado con BS + espacio + BS, como el editor del monitor.
  u.tap_clear();
  enviar(u, "PACMANX");
  enviar(u, std::string("") + (char)0x08 + " " + (char)0x08);
  enviar(u, "\r\n");
  comprobar("backspace", u.tap_text(), "PACMAN\n");

  // 3. ESC + 'H': el monitor manda esto al responder, para empezar limpio.
  u.tap_clear();
  enviar(u, "restos" + std::string("") + (char)0x1B + "Hola\r\n");
  comprobar("ESC", u.tap_text(), "Hola\n");

  // 4. CR solo: vuelve a la columna 0 y lo escrito después sobrescribe.
  //    Lo que quede a la derecha del cursor se mantiene hasta un borrado
  //    explícito (BS/ESC); es lo que hace un terminal tonto.
  u.tap_clear();
  enviar(u, "abcdef\rXY\r\n");
  comprobar("CR solo", u.tap_text(), "XYcdef\n");

  // 5. Bytes > 0x7F y controles: se descartan.
  u.tap_clear();
  enviar(u, "A" + std::string("") + (char)0xE1 + (char)0x01 + (char)0x07 + "B\r\n");
  comprobar("bytes altos", u.tap_text(), "AB\n");

  // 6. TAB expandido a múltiplo de 8.
  u.tap_clear();
  enviar(u, "ab\tX\r\n");
  comprobar("tab", u.tap_text(), "ab      X\n");

  // 7. DEL (0x7F) se comporta como BS.
  u.tap_clear();
  enviar(u, "AB" + std::string("") + (char)0x7F + "C\r\n");
  comprobar("DEL", u.tap_text(), "AC\n");

  // 8. La FIFO de transmisión no se ve afectada por el tap. Se vacía antes,
  //    porque las pruebas anteriores han dejado bytes en ella.
  while (u.has_tx()) u.dequeue_tx();
  u.tap_clear();
  enviar(u, "x");
  if (u.has_tx() && u.dequeue_tx() == 'x' && !u.has_tx()) {
    std::printf("  OK    fifo de TX intacta\n");
  } else {
    std::printf("  FALLO fifo de TX\n");
    fallos++;
  }

  std::printf(fallos ? "\n%d FALLO(S)\n" : "\nTodo correcto\n", fallos);
  return fallos ? 1 : 0;
}
