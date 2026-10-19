#include "tcp_server.h"

#include <cstdio>

#ifdef _WIN32
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <errno.h>
  #define closesocket(s) ::close(s)
#endif

TcpServer::~TcpServer() { stop(); }

#ifdef _WIN32
static bool winsock_ready = false;
static bool init_winsock() {
  if (winsock_ready) return true;
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
  winsock_ready = true;
  return true;
}
#endif

void TcpServer::set_nonblocking(socket_t s) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(s, FIONBIO, &mode);
#else
  int flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

bool TcpServer::start(int port) {
#ifdef _WIN32
  if (!init_winsock()) return false;
#endif

  listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_ == INVALID_SOCKET) return false;

  int one = 1;
  setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((unsigned short)port);

  if (::bind(listen_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
    std::fprintf(stderr, "TCP bind falló (¿puerto %d en uso?)\n", port);
    closesocket(listen_);
    listen_ = INVALID_SOCKET;
    return false;
  }

  if (::listen(listen_, 1) == SOCKET_ERROR) {
    std::fprintf(stderr, "TCP listen falló\n");
    closesocket(listen_);
    listen_ = INVALID_SOCKET;
    return false;
  }

  set_nonblocking(listen_);
  std::printf("UART emulado escuchando en localhost:%d (PuTTY modo Raw)\n", port);
  return true;
}

void TcpServer::stop() {
  close_client();
  if (listen_ != INVALID_SOCKET) {
    closesocket(listen_);
    listen_ = INVALID_SOCKET;
  }
}

void TcpServer::close_client() {
  if (client_ != INVALID_SOCKET) {
    closesocket(client_);
    client_ = INVALID_SOCKET;
    std::printf("Cliente desconectado\n");
  }
}

void TcpServer::poll() {
  // Aceptar una conexión pendiente (no bloqueante).
  if (client_ == INVALID_SOCKET) {
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    socket_t c = ::accept(listen_, (sockaddr*)&peer, &len);
    if (c != INVALID_SOCKET) {
      set_nonblocking(c);
      client_ = c;
      std::printf("Cliente conectado\n");
    }
  }

  if (client_ != INVALID_SOCKET) {
    // --- Datos entrantes (PuTTY -> UART) ---
    uint8_t buf[256];
    int n = ::recv(client_, (char*)buf, sizeof(buf), 0);
    if (n > 0) {
      for (int i = 0; i < n; i++) {
        if (on_rx) on_rx(buf[i]);
      }
    } else if (n == 0) {
      close_client(); // el cliente cerró
    } else {
      // Sin datos (EAGAIN/EWOULDBLOCK) o error transitorio: seguir.
    }
  }

  // --- Datos salientes (UART -> PuTTY) ---
  //
  // Se acumulan los bytes del UART y se envían en bloques. Importante: una
  // secuencia ANSI (ESC[...) puede escribirse byte a byte por el software; si
  // cada byte saliera en un paquete TCP distinto, algunos terminales la
  // descartan (el ESC aislado se pierde). Se agrupan los bytes disponibles en
  // cada vuelta para que la secuencia viaje completa.
  if (client_ != INVALID_SOCKET && on_tx_available) {
    uint8_t block[1024];
    int count = 0;
    while (count < (int)sizeof(block) && on_tx_available()) {
      block[count++] = on_tx_dequeue();
    }
    if (count > 0) {
      int offset = 0;
      while (offset < count) {
        int sent = ::send(client_, (const char*)block + offset, count - offset, 0);
        if (sent <= 0) break; // sin espacio ahora o error: el resto se pierde
        offset += sent;
      }
    }
  }
}
