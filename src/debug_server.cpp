#include "debug_server.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "bus.h"
#include "debug_dump.h"
#include "sd_card.h"
#include "spi_sd.h"
#include "uart.h"
#include "video/video.h"

#include <string>

// Salto de linea para los mensajes de texto. Se usa una constante en vez de
// escribir la secuencia de escape en cada literal.
static const std::string NL = "\n";

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  using socklen_t = int;
  #define CLOSE_SOCK closesocket
  #define SOCK_ERR WSAGetLastError()
  #define EAGAIN_LIKE(e) ((e) == WSAEWOULDBLOCK)
  #define EINTR_LIKE(e)  ((e) == WSAEINTR)
  #define SHUT_WR_ SD_SEND
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCK ::close
  #define SOCK_ERR errno
  #define EAGAIN_LIKE(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
  #define EINTR_LIKE(e)  ((e) == EINTR)
  #define SHUT_WR_ SHUT_WR
#endif

namespace {

#ifdef _WIN32
bool wsa_ready() {
  static bool ok = false;
  if (!ok) {
    WSADATA w;
    ok = (WSAStartup(MAKEWORD(2, 2), &w) == 0);
  }
  return ok;
}
#endif

void set_nonblocking(long long s) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket((SOCKET)s, FIONBIO, &mode);
#else
  int fl = fcntl((int)s, F_GETFL, 0);
  fcntl((int)s, F_SETFL, fl | O_NONBLOCK);
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Conexión en curso.
//
// La versión síncrona original aceptaba, respondía y cerraba dentro de poll(),
// bloqueando el emulador mientras enviaba los ~230 KB del PNG. Además, cerraba
// con shutdown()+close() aunque el cliente todavía no hubiese mandado su FIN:
// quedaban datos sin leer en el buffer de recepción y Windows respondía RST
// ("ConnectionAbortedError / Remote end closed connection without response").
//
// Ahora cada conexión es una máquina de estados que avanza un poco en cada
// poll() y nunca bloquea. El ciclo de vida es:
//
//   LEYENDO   -> se lee la cabecera hasta \r\n\r\n
//   ENVIANDO  -> se manda la respuesta por tramos (encola y sigue en la
//                siguiente vuelta si el socket no acepta más)
//   PENDIENTE -> todo enviado: se espera a que el cliente lea y cierre (FIFO).
//                Aquí NO se hace shutdown(): en Windows descarta lo que aún
//                esté en el buffer de envío. Basta con leer hasta ver su FIN.
//   CERRANDO  -> se cierra el socket. La opción SO_LINGER con 1 s evita que un
//                FIN perdido se convierta en RST.
//
// Hay un límite de tiempo por fase para que un cliente colgado no ocupe hueco.
// ---------------------------------------------------------------------------

struct DebugServer::Conn {
  enum Estado { LEYENDO, ENVIANDO, PENDIENTE, CERRANDO };

  long long sock = kInvalid;
  Estado estado = LEYENDO;
  std::string in;          // petición recibida
  std::string out;         // respuesta pendiente de enviar (cabecera + cuerpo)
  size_t sent = 0;
  long long created = 0;   // instante de aceptación
  long long fase = 0;      // instante de entrada en el estado PENDIENTE
};

long long DebugServer::now_ms() {
#ifdef _WIN32
  return (long long)GetTickCount();
#else
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

DebugServer::DebugServer() = default;

DebugServer::~DebugServer() { stop(); }

bool DebugServer::start(int port, Bus& bus) {
#ifdef _WIN32
  if (!wsa_ready()) {
    std::fprintf(stderr, "debug: WSAStartup fallo\n");
    return false;
  }
#endif
  bus_ = &bus;
  port_ = port;

  // Dos sockets de escucha: uno IPv4 y otro IPv6, ambos SOLO en loopback.
  //
  // Motivo: los clientes resuelven 'localhost' primero a ::1 (IPv6). Si el
  // servidor solo escuchaba en 127.0.0.1, el cliente esperaba al timeout de la
  // conexión IPv6 antes de reintentar en IPv4: urllib tardaba más de 2 segundos
  // por petición en vez de 20 ms.
  //
  // No sirve un único socket IPv6 con IPV6_V6ONLY=0: Windows no mapea
  // 127.0.0.1 a ::ffff:127.0.0.1, así que seguiría sin atender IPv4.
  //
  // Para que dos sockets compartan puerto hacen falta SO_REUSEADDR en ambos y
  // IPV6_V6ONLY=1 en el de IPv6. Sigue sin estar expuesto a la red: solo
  // direcciones de loopback.
  int one = 1;
  auto abrir = [&](int familia) -> long long {
    long long sk = (long long)::socket(familia, SOCK_STREAM, 0);
    if (sk == kInvalid) return kInvalid;
    setsockopt((SOCKET)sk, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

    int rc;
    if (familia == AF_INET6) {
#ifdef _WIN32
      int v6only = 1; // socket independiente del IPv4
      setsockopt((SOCKET)sk, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6only, sizeof(v6only));
#endif
      sockaddr_in6 a{};
      a.sin6_family = AF_INET6;
      a.sin6_addr = in6addr_loopback;
      a.sin6_port = htons((unsigned short)port);
      rc = ::bind((SOCKET)sk, (sockaddr*)&a, sizeof(a));
    } else {
      sockaddr_in a{};
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      a.sin_port = htons((unsigned short)port);
      rc = ::bind((SOCKET)sk, (sockaddr*)&a, sizeof(a));
    }

    if (rc != 0 || ::listen((SOCKET)sk, 8) != 0) {
      CLOSE_SOCK((SOCKET)sk);
      return kInvalid;
    }
    set_nonblocking(sk);
    return sk;
  };

  listen_ = abrir(AF_INET);
  listen6_ = abrir(AF_INET6);

  if (listen_ == kInvalid && listen6_ == kInvalid) {
    std::fprintf(stderr, "debug: no se pudo abrir el puerto %d (ocupado?)\n", port);
    return false;
  }

  std::printf("Debug HTTP: http://localhost:%d  (pantalla, VRAM, estado, RAM)\n", port);
  return true;
}

void DebugServer::stop() {
  for (auto& c : conns_) {
    if (c->sock != kInvalid) CLOSE_SOCK((SOCKET)c->sock);
    c->sock = kInvalid;
  }
  conns_.clear();
  if (listen_ != kInvalid) {
    CLOSE_SOCK((SOCKET)listen_);
    listen_ = kInvalid;
  }
  if (listen6_ != kInvalid) {
    CLOSE_SOCK((SOCKET)listen6_);
    listen6_ = kInvalid;
  }
}

void DebugServer::poll() {
  if (listen_ == kInvalid && listen6_ == kInvalid) return;
  // 1) Aceptar conexiones nuevas en ambos sockets (IPv4 e IPv6).
  for (int i = 0; i < 8; i++) {
    bool algo = false;
    long long listeners[2] = {listen_, listen6_};
    for (long long ls : listeners) {
      if (ls == kInvalid) continue;
      SOCKET s = ::accept((SOCKET)ls, nullptr, nullptr);
      if ((long long)s == kInvalid) continue;
      algo = true;
      set_nonblocking((long long)s);
      // SO_LINGER con tiempo: si por algún motivo el FIN no llega, el cierre
      // espera a entregar los datos pendientes en vez de abortar con RST.
#ifdef _WIN32
      linger lg{};
      lg.l_onoff = 1;
      lg.l_linger = 1;
      setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&lg, sizeof(lg));
#endif
      auto c = std::make_unique<Conn>();
      c->sock = (long long)s;
      c->created = now_ms();
      conns_.push_back(std::move(c));
    }
    if (!algo) break;
  }

  // Tope de seguridad: si un cliente se queda colgado, no acumular.
  while (conns_.size() > 8) {
    CLOSE_SOCK((SOCKET)conns_.front()->sock);
    conns_.erase(conns_.begin());
  }

  // 2) Avanzar cada conexión.
  for (auto it = conns_.begin(); it != conns_.end();) {
    if (step(**it)) {
      ++it;
    } else {
      CLOSE_SOCK((SOCKET)(*it)->sock);
      it = conns_.erase(it);
    }
  }
}

// Devuelve false cuando la conexión ya se ha cerrado y hay que liberarla.
bool DebugServer::step(Conn& c) {
  char buf[4096];
  const long long ahora = now_ms();

  switch (c.estado) {
    case Conn::LEYENDO: {
      for (int i = 0; i < 4; i++) {
        int n = ::recv((SOCKET)c.sock, buf, sizeof(buf), 0);
        if (n > 0) {
          c.in.append(buf, (size_t)n);
          continue;
        }
        if (n == 0) return false; // el cliente cerró sin llegar a pedir nada
        const int e = SOCK_ERR;
        if (EAGAIN_LIKE(e) || EINTR_LIKE(e)) break;
        return false;
      }
      if (ahora - c.created > 5000) return false; // petición incompleta

      const size_t end = c.in.find("\r\n\r\n");
      if (end == std::string::npos) {
        return c.in.size() <= 65536; // esperar el resto de la cabecera
      }

      // POST: hay que esperar a que llegue el cuerpo entero (Content-Length).
      // Lo usa /sdcard/write para inyectar binarios.
      {
        const std::string cab = c.in.substr(0, end);
        if (cab.compare(0, 5, "POST ") == 0) {
          size_t len = 0;
          const size_t p = cab.find("Content-Length:");
          if (p != std::string::npos) {
            len = (size_t)std::strtoul(cab.c_str() + p + 15, nullptr, 10);
          }
          const size_t completo = end + 4 + len;
          if (c.in.size() < completo) {
            return c.in.size() <= (16u * 1024u * 1024u);  // tope: 16 MB
          }
          cuerpo_peticion_.assign(c.in, end + 4, len);
        }
      }

      build_response(c, c.in.substr(0, end));
      std::string full = "HTTP/1.1 " + resp_.status + "\r\nContent-Type: " + resp_.type +
                         "\r\nContent-Length: " + std::to_string(resp_.body.size()) +
                         "\r\nCache-Control: no-store\r\n" + resp_.extra_headers +
                         "Connection: close\r\n\r\n";
      full += resp_.body;
      c.out.swap(full);
      c.in.clear();
      c.in.shrink_to_fit();
      c.estado = Conn::ENVIANDO;
      [[fallthrough]];
    }

    case Conn::ENVIANDO: {
      // Se envía por tramos; si el socket no acepta más, se reintenta en la
      // siguiente vuelta del bucle (la emulación no se detiene).
      for (int i = 0; i < 16 && c.sent < c.out.size(); i++) {
        const size_t left = c.out.size() - c.sent;
        const int chunk = left > 65536 ? 65536 : (int)left;
        const int n = ::send((SOCKET)c.sock, c.out.data() + c.sent, chunk, 0);
        if (n > 0) {
          c.sent += (size_t)n;
          continue;
        }
        const int e = SOCK_ERR;
        if (EAGAIN_LIKE(e) || EINTR_LIKE(e)) break;
        return false; // el cliente se fue
      }
      if (c.sent < c.out.size()) return ahora - c.created < 60000;

      // Todo enviado. Se cierra la nosa mitad (FIN) INMEDIATAMENTE.
      //
      // Antes se esperaba al FIN del cliente sin enviar el nuestro, y eso es un
      // interbloqueo: el cliente (urllib, curl) espera a que el servidor cierre
      // porque se anunció "Connection: close", y el servidor esperaba al FIN
      // del cliente. Resultado: cada petición tardaba segundos (o se quedaba
      // colgada hasta el timeout).
      ::shutdown((SOCKET)c.sock, SHUT_WR_);

      c.out.clear();
      c.out.shrink_to_fit();
      c.estado = Conn::PENDIENTE;
      c.fase = ahora;
      [[fallthrough]];
    }

    case Conn::PENDIENTE: {
      // Ya hay FIN enviado. Se drena lo que envíe el cliente para que el cierre
      // no acabe en RST (con datos sin leer en el buffer de recepción, Windows
      // responde con un reset y el cliente pierde la respuesta).
      for (int i = 0; i < 8; i++) {
        const int n = ::recv((SOCKET)c.sock, buf, sizeof(buf), 0);
        if (n > 0) continue;    // basura extra del cliente: se descarta
        if (n == 0) return false; // FIN del cliente: cerrar en orden
        const int e = SOCK_ERR;
        if (EAGAIN_LIKE(e) || EINTR_LIKE(e)) break;
        return false;
      }
      // Si el cliente no cierra (raro), soltar el hueco pronto: la respuesta ya
      // está entregada y no gana nada esperando.
      return ahora - c.fase <= 500;
    }

    case Conn::CERRANDO:
      return false;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Rutas
// ---------------------------------------------------------------------------

void DebugServer::build_response(Conn& /*c*/, const std::string& head) {
  resp_ = Response{};
  resp_.status = "200 OK";
  resp_.type = "text/plain; charset=utf-8";

  // Primera línea: "GET /ruta?query HTTP/1.1"
  std::string line = head;
  const size_t eol = head.find("\r\n");
  if (eol != std::string::npos) line = head.substr(0, eol);

  std::string path;
  const size_t sp1 = line.find(' ');
  if (sp1 != std::string::npos) {
    const size_t sp2 = line.find(' ', sp1 + 1);
    path = line.substr(sp1 + 1, (sp2 == std::string::npos) ? std::string::npos : sp2 - sp1 - 1);
  }
  std::string query;
  const size_t q = path.find('?');
  if (q != std::string::npos) {
    query = path.substr(q + 1);
    path = path.substr(0, q);
  }

  // El monitor 6502 cachea el directorio de la MicroFS al montar la tarjeta.
  // Tras tocar la imagen desde aqui hay que mandarle `sd\r` por el UART para
  // que lo relea; si no, su LS sigue mostrando la lista vieja. Las rutas de
  // /sdcard lo hacen solas salvo que se pase ?remount=0.
  auto remontar = [&]() {
    if (Uart* u = bus_->uart()) {
      for (char c : std::string("sd\r")) u->enqueue_rx((uint8_t)c);
    }
  };

  if (path == "/screen.png") {
    Video* v = bus_->video();
    if (!v) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin Core de Video\n";
      return;
    }
    int scale = 1;
    const size_t sq = query.find("scale=");
    if (sq != std::string::npos) {
      const int s = atoi(query.c_str() + sq + 6);
      if (s >= 1 && s <= 4) scale = s;
    }
    static std::vector<uint32_t> fb((size_t)Video::W * Video::H);
    v->render(fb.data());
    resp_.type = "image/png";
    debug_dump::screenshot_png_mem(fb.data(), Video::W, Video::H, scale, resp_.body);
    if (resp_.body.empty()) {
      resp_.status = "500 Internal Server Error";
      resp_.type = "text/plain";
      resp_.body = "error generando PNG\n";
    }

  } else if (path == "/screen.ppm") {
    // Formato sin compresión: útil para herramientas que no quieren PNG.
    Video* v = bus_->video();
    if (!v) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin Core de Video\n";
      return;
    }
    static std::vector<uint32_t> fb((size_t)Video::W * Video::H);
    v->render(fb.data());
    resp_.type = "image/x-portable-pixmap";
    debug_dump::screenshot_ppm_mem(fb.data(), Video::W, Video::H, resp_.body);

  } else if (path == "/sdcard/ls") {
    // Listado de la tarjeta, en texto (una linea por archivo).
    if (!sd_ || !sd_->montada()) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin tarjeta SD" + NL;
    } else {
      resp_.type = "text/plain";
      resp_.body = "libre: " + std::to_string(sd_->espacio_libre()) + " bytes" + NL;
      for (const auto& e : sd_->listar_archivos()) {
        resp_.body += e.nombre + "  " + std::to_string(e.bytes) + " bytes  sector " +
                      std::to_string(e.sector) + NL;
      }
    }

  } else if (path == "/sdcard/write") {
    // POST /sdcard/write?name=JUEGO.BIN  (cuerpo = bytes del archivo)
    //
    // Se espera a que la CPU no este usando el SPI para no pisar una
    // transferencia en curso.
    if (!sd_ || !sd_->montada()) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin tarjeta SD" + NL;
    } else if (bus_->spi() && bus_->spi()->ocupada()) {
      resp_.status = "409 Conflict";
      resp_.body = "la SD esta ocupada por la CPU; reintenta" + NL;
    } else {
      std::string nombre;
      const size_t pn = query.find("name=");
      if (pn != std::string::npos) {
        nombre = query.substr(pn + 5);
        const size_t amp = nombre.find('&');
        if (amp != std::string::npos) nombre = nombre.substr(0, amp);
      }
      std::string err;
      const std::vector<uint8_t> datos(cuerpo_peticion_.begin(), cuerpo_peticion_.end());
      if (nombre.empty()) {
        resp_.status = "400 Bad Request";
        resp_.body = "uso: POST /sdcard/write?name=NOMBRE.BIN (cuerpo = datos)" + NL;
      } else if (sd_->agregar_archivo(nombre, datos, &err)) {
        resp_.body = "OK: " + nombre + " (" + std::to_string(datos.size()) + " bytes)" + NL;
        std::printf("SD: %s inyectado en caliente (%zu bytes)\n", nombre.c_str(),
                    datos.size());
        if (query.find("remount=0") == std::string::npos) {
          remontar();
          resp_.body += "remontado: sd\n";
        }
      } else {
        resp_.status = "409 Conflict";
        resp_.body = "ERROR: " + err + NL;
      }
    }

  } else if (path == "/sdcard/rename") {
    // /sdcard/rename?name=VIEJO.BIN&to=NUEVO.BIN
    if (!sd_ || !sd_->montada()) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin tarjeta SD" + NL;
    } else {
      std::string viejo, nuevo;
      const size_t pn = query.find("name=");
      if (pn != std::string::npos) {
        viejo = query.substr(pn + 5);
        const size_t amp = viejo.find('&');
        if (amp != std::string::npos) viejo = viejo.substr(0, amp);
      }
      const size_t pt = query.find("to=");
      if (pt != std::string::npos) nuevo = query.substr(pt + 3);
      std::string err;
      if (viejo.empty() || nuevo.empty()) {
        resp_.status = "400 Bad Request";
        resp_.body = "uso: /sdcard/rename?name=VIEJO.BIN&to=NUEVO.BIN" + NL;
      } else if (sd_->renombrar_archivo(viejo, nuevo, &err)) {
        resp_.body = "OK: " + viejo + " -> " + nuevo + NL;
        if (query.find("remount=0") == std::string::npos) {
          remontar();
          resp_.body += "remontado: sd\n";
        }
      } else {
        resp_.status = "404 Not Found";
        resp_.body = "ERROR: " + err + NL;
      }
    }

  } else if (path == "/sdcard/delete") {
    if (!sd_ || !sd_->montada()) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin tarjeta SD" + NL;
    } else if (bus_->spi() && bus_->spi()->ocupada()) {
      resp_.status = "409 Conflict";
      resp_.body = "la SD esta ocupada por la CPU; reintenta" + NL;
    } else {
      std::string nombre;
      const size_t pn = query.find("name=");
      if (pn != std::string::npos) nombre = query.substr(pn + 5);
      std::string err;
      if (nombre.empty()) {
        resp_.status = "400 Bad Request";
        resp_.body = "uso: /sdcard/delete?name=NOMBRE.BIN" + NL;
      } else if (sd_->borrar_archivo(nombre, &err)) {
        resp_.body = "OK: " + nombre + " eliminado" + NL;
        if (query.find("remount=0") == std::string::npos) {
          remontar();
          resp_.body += "remontado: sd\n";
        }
      } else {
        resp_.status = "404 Not Found";
        resp_.body = "ERROR: " + err + NL;
      }
    }

  } else if (path == "/sdcard/read") {
    // /sdcard/read?name=X.BIN  -> los bytes del archivo
    //
    // Cierra el ciclo por HTTP: escribir con /sdcard/write, comprobar lo que ha
    // generado el ensamblador o el propio monitor, y compararlo sin abrir la
    // imagen con herramientas externas (que además pelean con el emulador por
    // el fichero).
    if (!sd_ || !sd_->montada()) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin tarjeta SD" + NL;
    } else {
      std::string nombre;
      const size_t pn = query.find("name=");
      if (pn != std::string::npos) {
        nombre = query.substr(pn + 5);
        const size_t amp = nombre.find('&');
        if (amp != std::string::npos) nombre = nombre.substr(0, amp);
      }
      std::vector<uint8_t> datos;
      std::string err;
      if (nombre.empty()) {
        resp_.status = "400 Bad Request";
        resp_.body = "uso: /sdcard/read?name=NOMBRE.BIN" + NL;
      } else if (sd_->leer_archivo(nombre, &datos, &err)) {
        resp_.type = "application/octet-stream";
        resp_.body.assign(datos.begin(), datos.end());
        // Cabecera util para scripts: el tamano sin tener que mirar el cuerpo.
        resp_.extra_headers =
            "X-File-Name: " + nombre + "\r\nX-File-Size: " +
            std::to_string(datos.size()) + "\r\n";
      } else {
        resp_.status = "404 Not Found";
        resp_.body = "ERROR: " + err + NL;
      }
    }

  } else if (path == "/sdcard/remount") {
    // Le manda `sd\r` al monitor (lo mismo que el boton `Remontar` de la
    // ventana). Solo hace falta si se ha tocado la imagen por fuera o se uso
    // ?remount=0 en write/delete/rename.
    if (!sd_ || !sd_->montada()) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin tarjeta SD" + NL;
    } else if (bus_->uart()) {
      remontar();
      resp_.body = "OK: re-montaje pedido al monitor (sd)" + NL;
      std::printf("SD: re-montaje pedido al monitor (HTTP)\n");
    } else {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin UART" + NL;
    }

  } else if (path == "/uart") {
    // Inyecta texto en el UART como si llegara de PuTTY.
    //
    //   GET  /uart?data=ls%0D
    //   POST /uart?data=ls%0D           (o GET con ?data=)
    //   POST /uart                       cuerpo = bytes crudos
    //   GET  /uart?cmd=ls               añade el \r solo
    //
    // `data` y `cmd` admiten escapes de URL (%0D, %0A, %20...); el cuerpo del
    // POST se toma tal cual, byte a byte (útil para el editor de la ROM o
    // para mandar binario). Se responde de inmediato, SIN esperar la salida
    // del monitor: para leer la respuesta, consulta /console.txt.
    if (!bus_->uart()) {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin UART" + NL;
    } else {
      // Decodifica %XX y '+' (como un formulario).
      auto url_decode = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
          if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
              if (c >= '0' && c <= '9') return c - '0';
              if (c >= 'a' && c <= 'f') return c - 'a' + 10;
              if (c >= 'A' && c <= 'F') return c - 'A' + 10;
              return -1;
            };
            const int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
              out.push_back((char)((hi << 4) | lo));
              i += 2;
              continue;
            }
          }
          out.push_back(s[i] == '+' ? ' ' : s[i]);
        }
        return out;
      };

      // Valor de un parametro de la query (?clave=...).
      auto param = [&](const char* clave) -> std::string {
        const std::string k = std::string(clave) + "=";
        const size_t p = query.find(k);
        if (p == std::string::npos) return std::string();
        const size_t ini = p + k.size();
        const size_t fin = query.find('&', ini);
        return query.substr(ini, fin == std::string::npos ? std::string::npos
                                                         : fin - ini);
      };

      // Un `?cmd=` vacío mandaba solo el Enter, y el monitor respondía `ERR: `
      // a una línea vacía (que además deja la pantalla llena de errores no es
      // lo que pide quien escribe la URL). Se rechaza con un mensaje claro.
      const bool pide_cmd = query.find("cmd=") != std::string::npos;
      const bool pide_data = query.find("data=") != std::string::npos;
      std::string texto;
      bool con_retorno = false;
      const std::string data = param("data");
      const std::string cmd = param("cmd");
      if (!data.empty()) {
        texto = url_decode(data);
      } else if (!cmd.empty()) {
        texto = url_decode(cmd);
        con_retorno = true;  // atajo: equivale a escribir y pulsar Enter
      } else if (!cuerpo_peticion_.empty()) {
        texto.assign(cuerpo_peticion_.begin(), cuerpo_peticion_.end());
      }

      if (texto.empty()) {
        resp_.status = "400 Bad Request";
        if (pide_cmd || pide_data) {
          resp_.body = "ERROR: " + std::string(pide_cmd ? "cmd" : "data") +
                       " esta vacio (no se envia nada al monitor)" + NL;
        } else {
          resp_.body =
              "uso: GET/POST /uart?data=TEXTO (%0D para Enter), ?cmd=TEXTO "
              "(anade Enter) o POST con el cuerpo = bytes crudos" + NL;
        }
      } else {
        Uart* u = bus_->uart();
        for (char c : texto) u->enqueue_rx((uint8_t)c);
        if (con_retorno) u->enqueue_rx('\r');
        resp_.body = "OK: " + std::to_string(texto.size() + (con_retorno ? 1 : 0)) +
                     " bytes al UART. La respuesta sale en /console.txt" + NL;
        std::printf("UART: %zu bytes inyectados por HTTP\n",
                    texto.size() + (con_retorno ? 1 : 0));
      }
    }

  } else if (path == "/console.txt") {
    // Pantalla de la consola (80x24) tal como la interpreta el terminal del
    // panel: útil para que un agente compruebe qué está mostrando un programa
    // de texto sin abrir la ventana.
    if (Uart* u = bus_->uart()) {
      resp_.type = "text/plain; charset=us-ascii";
      resp_.body = u->terminal().a_texto();
    } else {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin UART\n";
    }

  } else if (path == "/console.png") {
    // Captura de la consola, con colores y glifos CP437, como la ve el usuario.
    if (Uart* u = bus_->uart()) {
      int scale = 1;
      const size_t sq = query.find("scale=");
      if (sq != std::string::npos) {
        const int s = atoi(query.c_str() + sq + 6);
        if (s >= 1 && s <= 4) scale = s;
      }
      std::vector<uint32_t> px;
      int W = 0, H = 0;
      u->terminal().a_imagen(&px, &W, &H);
      resp_.type = "image/png";
      debug_dump::screenshot_png_mem(px.data(), W, H, scale, resp_.body);
    } else {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin UART\n";
    }

  } else if (path == "/vram.txt") {
    if (Video* v = bus_->video()) {
      debug_dump::dump_vram_txt_mem(*v, resp_.body);
    } else {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin Core de Video\n";
    }

  } else if (path == "/state.txt") {
    debug_dump::dump_state_txt_mem(*bus_, cpu_, resp_.body);

  } else if (path == "/ram.bin") {
    // Volcado de los 16 KB de RAM (formato binario: pantalla cruda).
    const size_t start = query.find("start=");
    const size_t len_q = query.find("len=");
    unsigned a = 0, l = 16384;
    if (start != std::string::npos) a = (unsigned)strtoul(query.c_str() + start + 6, nullptr, 0);
    if (len_q != std::string::npos) l = (unsigned)strtoul(query.c_str() + len_q + 4, nullptr, 0);
    resp_.type = "application/octet-stream";
    debug_dump::dump_ram_bin_mem(*bus_, a, l, resp_.body);

  } else if (path == "/vram.json") {
    // Tilemap y OAM en JSON: lo más cómodo para leerlo desde un agente.
    if (Video* v = bus_->video()) {
      resp_.type = "application/json; charset=utf-8";
      debug_dump::dump_video_json_mem(*v, resp_.body);
    } else {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin Core de Video\n";
    }

  } else if (path == "/" || path == "/index.html") {
    resp_.type = "text/html; charset=utf-8";
    resp_.body = index_html();

  } else if (path == "/favicon.ico") {
    resp_.status = "204 No Content";
    resp_.type = "image/x-icon";

  } else {
    resp_.status = "404 Not Found";
    resp_.body =
        "rutas disponibles:\n"
        "  /                indice con vista previa en vivo\n"
        "  /screen.png      captura de lo que saldria por HDMI\n"
        "  /screen.ppm      igual, sin comprimir\n"
        "  /vram.txt        tilemap + atributos + OAM en texto\n"
        "  /vram.json       tilemap + atributos + OAM en JSON\n"
        "  /state.txt       LEDs, SID, CPU, estado\n"
        "  /ram.bin?start=0&len=16384  volcado de RAM\n";
  }
}

std::string DebugServer::index_html() const {
  return
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<title>Emulador FPGA 6502 - debug</title>"
      "<style>"
      "body{font-family:system-ui;background:#1e2127;color:#ddd;padding:20px;margin:0}"
      "h2{margin-top:0}a{color:#7ec8ff;margin-right:12px}"
      "img{image-rendering:pixelated;border:1px solid #444;max-width:100%;background:#000}"
      "pre{background:#111;padding:10px;overflow:auto;max-height:60vh;font-size:11px;"
      "line-height:1.2}td,th{padding:2px 6px;text-align:left;vertical-align:top}"
      "</style></head><body>"
      "<h2>Emulador FPGA 6502 &mdash; depuraci&oacute;n</h2>"
      "<table><tr><td><img id=\"img\" src=\"/screen.png?scale=2\" width=\"960\" height=\"720\"></td>"
      "<td><pre id=\"state\" style=\"width:420px;max-height:720px\"></pre></td></tr></table>"
      "<p><a href=\"/screen.png\">/screen.png</a>"
      "<a href=\"/vram.txt\">/vram.txt</a>"
      "<a href=\"/vram.json\">/vram.json</a>"
      "<a href=\"/state.txt\">/state.txt</a>"
      "<a href=\"/ram.bin\">/ram.bin</a></p>"
      "<script>"
      "const t=(d)=>Date.now()+'&t='+d;"
      "setInterval(()=>{document.getElementById('img').src='/screen.png?scale=2&t='+Date.now()},500);"
      "setInterval(async()=>{const r=await fetch('/state.txt?t='+Date.now());"
      "document.getElementById('state').textContent=await r.text();},700);"
      "</script></body></html>";
}
