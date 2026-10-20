#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "device.h"

// UART fiel al uart.vhd del fpga-6502-16k.
//
// Registros ($C020-$C023):
//   $C020  (W) TX_DATA  -> encola un byte para transmitir
//   $C020  (R) RX_DATA  -> byte recibido; al leer se limpia RX_VALID
//   $C021  (R) STATUS   -> OVERRUN(4) ERROR(3) TX_BUSY(2) RX_VALID(1) TX_READY(0)
//   $C021  (W) CONTROL  -> bit0 TX_IRQ_EN, bit1 RX_IRQ_EN, bit7 RESET_FLAGS
//   $C022  (R/W) BAUD_LO
//   $C023  (R/W) BAUD_HI
//
// El divisor por defecto es 58 (115200 @ 6.75 MHz).
class Uart : public Device {
public:
  Uart();

  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Cola de transmisión hacia el transporte (socket). Es una FIFO para no
  // perder la salida del monitor si el host aún no ha conectado (el hardware
  // dejaría el byte en el cable; aquí lo guardamos hasta que se pueda enviar).
  bool has_tx() const { return !tx_fifo_.empty(); }
  uint8_t dequeue_tx();

  // Tap de SOLO LECTURA para observar lo transmitido sin consumirlo.
  //
  // Lo usa el terminal de la ventana: copia de lo que sale hacia PuTTY, sin
  // interferir con la FIFO. Si consumiera de `tx_fifo_`, PuTTY dejaría de
  // recibir datos.
  // Devuelve cuántos bytes quedan sin leer del flujo observado.
  size_t tap_pending() const { return tap_.size(); }
  uint8_t tap_pop();          // saca el siguiente byte pendiente (0 si no hay)
  const std::string& tap_text() const { return tap_text_; }
  void tap_clear() { tap_.clear(); tap_text_.clear(); }

  // Entrada desde el host (PuTTY). Si RX_VALID ya estaba activo, marca overrun.
  void enqueue_rx(uint8_t v);

  // Avanza el temporizador de recepción serial. "cycles" son ciclos de CPU
  // (3.375 MHz). Sin esto, todos los bytes llegarían "ya recibidos" a la vez y
  // el monitor (que sondea cuántos quedan antes de leer) cortaría el eco.
  void tick(uint32_t cycles);

  // Línea IRQ (activa alta): (tx_ready & tx_irq_en) | (rx_valid & rx_irq_en)
  bool irq() const { return irq_; }

private:
  void start_next_frame();
  static constexpr size_t TX_FIFO_MAX = 65536;

  // Receptor
  uint8_t rx_buffer_  = 0;
  bool    rx_valid_   = false;
  bool    rx_error_   = false;
  bool    rx_overrun_ = false;
  std::deque<uint8_t> rx_fifo_; // bytes esperando a ser entregados al CPU
  uint32_t serial_countdown_ = 0; // ciclos hasta publicar el byte en curso
  bool     rx_in_flight_     = false; // hay una trama serial recibiéndose

  // Transmisor
  std::deque<uint8_t> tx_fifo_;
  bool tx_ready_ = true;
  bool tx_irq_pending_ = false; // flanco: se acaba de completar una transmisión

  // Flujo observado para el terminal de la ventana (ver tap_pending()).
  std::deque<uint8_t> tap_;
  std::string tap_text_;      // últimas líneas, ya con el formato del terminal
  static constexpr size_t TAP_MAX = 64 * 1024;   // tope por si nadie lo vacía
  static constexpr size_t TAP_TEXT_MAX = 32 * 1024;
  void tap_push(uint8_t v);   // alimenta el tap (lo llama write de TX)

  // Control / baudrate
  bool     tx_irq_en_ = false;
  bool     rx_irq_en_ = false;
  uint16_t baud_div_  = 58; // 115200 @ 6.75 MHz

  bool irq_ = false;

  void update_irq();
};
