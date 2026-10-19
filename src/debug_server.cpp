#include "debug_server.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "bus.h"
#include "debug_dump.h"
#include "video/video.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  using socklen_t = int;
  #define CLOSE_SOCK closesocket
  #define SOCK_ERR WSAGetLastError()
  #define EAGAIN_LIKE(e) ((e) == WSAEWOULDBLOCK)
  #define EINTR_LIKE(e)  ((e) == WSAEINTR)
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCK ::close
  #define SOCK_ERR errno
  #define EAGAIN_LIKE(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
  #define EINTR_LIKE(e)  ((e) == EINTR)
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

  long long s = (long long)::socket(AF_INET, SOCK_STREAM, 0);
  if (s == kInvalid) return false;

  int one = 1;
  setsockopt((SOCKET)s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((unsigned short)port);

  if (::bind((SOCKET)s, (sockaddr*)&addr, sizeof(addr)) != 0) {
    std::fprintf(stderr, "debug: no se pudo abrir el puerto %d (ocupado?)\n", port);
    CLOSE_SOCK((SOCKET)s);
    return false;
  }
  if (::listen((SOCKET)s, 8) != 0) {
    CLOSE_SOCK((SOCKET)s);
    return false;
  }
  set_nonblocking(s);
  listen_ = s;

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
}

void DebugServer::poll() {
  if (listen_ == kInvalid) return;

  // 1) Aceptar conexiones nuevas. Se atienden varias a la vez para que el
  //    polling de la página de índice (una petición cada 500 ms) y una captura
  //    pedida por una herramienta externa no se pisen.
  for (int i = 0; i < 8; i++) {
    SOCKET s = ::accept((SOCKET)listen_, nullptr, nullptr);
    if ((long long)s == kInvalid) break;
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

      build_response(c, c.in.substr(0, end));
      std::string full = "HTTP/1.1 " + resp_.status + "\r\nContent-Type: " + resp_.type +
                         "\r\nContent-Length: " + std::to_string(resp_.body.size()) +
                         "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
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

      c.out.clear();
      c.out.shrink_to_fit();
      c.estado = Conn::PENDIENTE;
      c.fase = ahora;
      [[fallthrough]];
    }

    case Conn::PENDIENTE: {
      // Ya está todo entregado al kernel. Leer hasta ver el FIN del cliente:
      // al leer todo, el cierre posterior no genera RST.
      // El cliente puede tardar: urllib lee en su propio proceso y cierra
      // unos milisegundos después. Por eso este margen es amplio; al recibir
      // el FIN se cierra de inmediato.
      for (int i = 0; i < 8; i++) {
        const int n = ::recv((SOCKET)c.sock, buf, sizeof(buf), 0);
        if (n > 0) continue;
        if (n == 0) {
          c.estado = Conn::CERRANDO;
          return false;
        }
        const int e = SOCK_ERR;
        if (EAGAIN_LIKE(e) || EINTR_LIKE(e)) break;
        return false;
      }
      // HTTP/1.0 que no cierra nunca: liberar el hueco pasado un tiempo.
      return ahora - c.fase <= 10000;
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

  } else if (path == "/vram.txt") {
    if (Video* v = bus_->video()) {
      debug_dump::dump_vram_txt_mem(*v, resp_.body);
    } else {
      resp_.status = "503 Service Unavailable";
      resp_.body = "sin Core de Video\n";
    }

  } else if (path == "/state.txt") {
    debug_dump::dump_state_txt_mem(*bus_, resp_.body);

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
