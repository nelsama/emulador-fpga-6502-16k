#pragma once

// Servidor TCP local para conectar PuTTY (modo Raw) al UART emulado.
//
// Todos los sockets son NO BLOQUEANTES: poll() nunca se queda esperando, de
// modo que el emulador sigue ejecutando el CPU aunque no haya cliente.

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET socket_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
typedef int socket_t;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR (-1)
#endif

#include <cstdint>
#include <functional>

class TcpServer {
public:
  TcpServer() = default;
  ~TcpServer();

  bool start(int port = 4000);
  void stop();

  // Llamado desde el bucle principal. Nunca bloquea.
  void poll();

  // Callbacks conectados al UART.
  std::function<void(uint8_t)> on_rx;
  std::function<bool()>        on_tx_available;
  std::function<uint8_t()>     on_tx_dequeue;

  bool connected() const { return client_ != INVALID_SOCKET; }

private:
  void set_nonblocking(socket_t s);
  void close_client();

  socket_t listen_ = INVALID_SOCKET;
  socket_t client_ = INVALID_SOCKET;
};
