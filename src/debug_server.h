#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Bus;

// Servidor HTTP para observar el emulador desde fuera: una IA, un navegador o
// un script pueden pedir la pantalla, la VRAM, el estado y la RAM sin detener
// la emulación. Es la vía de depuración del software que se ejecuta en la
// máquina emulada.
//
//   GET /                 índice con vista previa en vivo y estado
//   GET /screen.png       captura exacta de lo que saldría por HDMI
//                         (&scale=1..4 para ampliar)
//   GET /screen.ppm       la misma imagen sin comprimir
//   GET /vram.txt         tilemap + atributos + OAM en texto
//   GET /vram.json        tilemap + atributos + OAM en JSON
//   GET /state.txt        CPU, LEDs, SID, estado del vídeo
//   GET /ram.bin         16 KB de RAM (&start=&len= para trozos)
//
// Arranca SIEMPRE con el emulador en el puerto 8080. Para cambiarlo:
//   emulador.exe rom.bin 4000 tarjeta.img --debug-http=8081
// o la variable de entorno EMU_DEBUG_PORT. Con 0 se desactiva.
//
// Las conexiones se atienden de forma incremental en poll(): generar y enviar
// una captura grande (230 KB) nunca bloquea el bucle de emulación.
class DebugServer {
public:
  DebugServer();
  ~DebugServer();

  bool start(int port, Bus& bus);
  void stop();
  void poll();

  int port() const { return port_; }
  bool active() const { return listen_ != kInvalid || listen6_ != kInvalid; }

private:
  struct Conn;
  struct Response {
    std::string status;
    std::string type;
    std::string body;
  };

  bool step(Conn& c);
  void build_response(Conn& c, const std::string& head);
  std::string index_html() const;

  static long long now_ms();

  static constexpr long long kInvalid = -1;
  long long listen_ = kInvalid;  // socket de escucha IPv4 (127.0.0.1)
  long long listen6_ = kInvalid; // socket de escucha IPv6 (::1)
  int port_ = 0;
  Bus* bus_ = nullptr;
  Response resp_;
  std::vector<std::unique_ptr<Conn>> conns_;
};
