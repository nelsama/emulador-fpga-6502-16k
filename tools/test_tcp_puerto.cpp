// Comprueba que el servidor TCP rechaza un puerto ya ocupado.
//
// En Windows, SO_REUSEADDR no significa "reusar un puerto en TIME_WAIT" sino que
// permite a un SEGUNDO proceso engancharse a un puerto que ya está escuchando:
// bind() no fallaba, el segundo emulador arrancaba convencido de que tenía el
// UART y las conexiones las atendía el primero. Había que usar
// SO_EXCLUSIVEADDRUSE.
#include <cstdio>

#include "tcp_server.h"

static int fallos = 0;
static void check(bool ok, const char* que) {
  std::printf("  %s  %s\n", ok ? "OK  " : "FALLO", que);
  if (!ok) fallos++;
}

int main() {
  std::printf("test_tcp_puerto: un puerto ocupado se rechaza\n");

  const int puerto = 4701;

  TcpServer primero;
  check(primero.start(puerto), "el primero abre el puerto");

  // El segundo debe fallar: es el caso de abrir run.bat dos veces.
  TcpServer segundo;
  const bool abrio = segundo.start(puerto);
  check(!abrio, "el segundo NO puede abrir el mismo puerto");

  primero.stop();

  // Con el primero cerrado, el puerto vuelve a estar disponible.
  TcpServer tercero;
  check(tercero.start(puerto), "tras cerrar el primero, se puede reabrir");
  tercero.stop();

  std::printf("test_tcp_puerto: %s\n", fallos ? "FALLOS" : "OK");
  return fallos ? 1 : 0;
}
