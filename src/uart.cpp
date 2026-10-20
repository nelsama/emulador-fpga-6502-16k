#include "uart.h"

// Ciclos de CPU que tarda una trama 8N1 (10 bits a 115200 baudios).
//
// El divisor del UART cuenta ciclos de 6.75 MHz (uart.vhd: BAUD_RATE=>115200,
// CLK_FREQ=>6_750_000 -> 58 ciclos por bit) y la CPU anda a 3.375 MHz, es
// decir, la mitad. Así que una trama son 10 x 58 / 2 = 290 ciclos de CPU.
static inline uint32_t BYTE_CYCLES(uint16_t divisor) {
  const uint32_t d = divisor ? divisor : 58;
  uint32_t c = 10 * (d / 2);
  return c ? c : 1;
}

Uart::Uart() {
  tx_ready_ = true;
}

bool Uart::decodes(uint16_t addr) const {
  return addr >= 0xC020 && addr <= 0xC023;
}

uint8_t Uart::read(uint16_t addr, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xC020: // RX_DATA: leer limpia RX_VALID (como el VHDL)
      // El monitor sondea $C021 justo después de leer (rutina getc en $80D3), y
      // en el FPGA esa consulta solo ve el byte siguiente cuando el receptor ha
      // terminado de recibirlo (~87 us después). Por eso NO se promociona el
      // siguiente byte de la FIFO aquí: se entrega en tick() cuando "termina" la
      // recepción serial. Al dejarlo libre, la trama del byte siguiente arranca.
      rx_valid_ = false;
      start_next_frame();
      update_irq();
      return rx_buffer_;
    case 0xC021: // STATUS
      // Nota: leer STATUS no limpia el flanco de IRQ de TX en este modelo
      // simplificado; el monitor usa polling de TX_READY, no la IRQ de TX.
      {
        uint8_t s = (uint8_t)(
          (rx_overrun_ ? 0x10 : 0x00) |
          (rx_error_   ? 0x08 : 0x00) |
          (0x00)                       | // TX_BUSY
          (rx_valid_   ? 0x02 : 0x00) |
          (tx_ready_   ? 0x01 : 0x00));
        return s;
      }
    case 0xC022: // BAUD_LO
      return (uint8_t)(baud_div_ & 0xFF);
    case 0xC023: // BAUD_HI
      return (uint8_t)((baud_div_ >> 8) & 0xFF);
    default:
      resp = false;
      return 0;
  }
}

void Uart::write(uint16_t addr, uint8_t data, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xC020: // TX_DATA
      // El hardware transmite el byte y vuelve a TX_READY en ~87 us. Aquí se
      // encola y TX_READY se mantiene en 1: el software nunca se bloquea,
      // igual que en el FPGA (que solo estaría "busy" esos 87 us).
      if (tx_fifo_.size() < TX_FIFO_MAX) {
        tx_fifo_.push_back(data);
      }
      // Copia para el terminal de la ventana. No consume la FIFO: PuTTY sigue
      // recibiendo lo mismo que antes.
      tap_push(data);
      tx_ready_ = true;
      tx_irq_pending_ = true; // flanco de "transmisión completada"
      update_irq();
      break;
    case 0xC021: // CONTROL
      tx_irq_en_ = (data & 0x01) != 0;
      rx_irq_en_ = (data & 0x02) != 0;
      if (data & 0x80) { // RESET_FLAGS
        rx_error_ = false;
        rx_overrun_ = false;
      }
      update_irq();
      break;
    case 0xC022: // BAUD_LO
      baud_div_ = (baud_div_ & 0xFF00) | data;
      break;
    case 0xC023: // BAUD_HI
      baud_div_ = (baud_div_ & 0x00FF) | ((uint16_t)data << 8);
      break;
    default:
      resp = false;
      break;
  }
}

uint8_t Uart::dequeue_tx() {
  uint8_t v = tx_fifo_.front();
  tx_fifo_.pop_front();
  return v;
}

void Uart::tap_push(uint8_t v) {
  // Cola para quien consuma byte a byte (si se quiere). Se descarta lo más
  // antiguo si nadie la vacía, para no crecer sin límite.
  if (tap_.size() >= TAP_MAX) tap_.pop_front();
  tap_.push_back(v);

  // Texto del terminal: se interpretan los saltos de línea y se recorta a las
  // últimas líneas. Es lo que pinta la ventana.
  //
  // El firmware manda "\r\n"; el '\r' se ignora (aquí no se sobreimprime
  // línea) y el '\n' abre línea nueva.
  if (v == '\n') {
    tap_text_.push_back('\n');
  } else if (v == '\r') {
    // ignorar
  } else if (v >= 0x20 || v == '\t') {
    tap_text_.push_back((char)v);
  }
  // Por encima del tope se tira lo más antiguo (por líneas completas).
  if (tap_text_.size() > TAP_TEXT_MAX) {
    const size_t corte = tap_text_.find('\n', tap_text_.size() - TAP_TEXT_MAX);
    if (corte != std::string::npos) tap_text_.erase(0, corte + 1);
    else tap_text_.erase(0, tap_text_.size() - TAP_TEXT_MAX);
  }
}

uint8_t Uart::tap_pop() {
  if (tap_.empty()) return 0;
  const uint8_t v = tap_.front();
  tap_.pop_front();
  return v;
}

void Uart::enqueue_rx(uint8_t v) {
  // Los bytes del host (PuTTY/TCP) llegan agrupados, pero en el cable entran
  // de uno en uno cada ~87 us. Se encolan y se publican al CPU a medida que
  // "termina" su recepción (tick), igual que el receptor del uart.vhd. Eso es
  // importante porque el monitor sondea cuántos bytes quedan pendientes antes
  // de leer el actual.
  // La FIFO tiene un tope: el software puede consumir mucho más despacio que el
  // cable (p. ej. mientras carga un SID), y sin límite el vector crecería sin
  // control. Si se llena, el byte se pierde (overrun, como en el hardware).
  static constexpr size_t RX_FIFO_MAX = 64 * 1024;
  if (rx_fifo_.size() >= RX_FIFO_MAX) {
    rx_overrun_ = true;
    return;
  }
  rx_fifo_.push_back(v);
  if (!rx_in_flight_) start_next_frame();
}

void Uart::tick(uint32_t cycles) {
  while (rx_in_flight_ && cycles >= serial_countdown_) {
    cycles -= serial_countdown_;

    // Fin de trama: el byte queda disponible en RX_BUFFER.
    rx_in_flight_ = false;
    if (rx_fifo_.empty()) break; // defensivo
    rx_buffer_ = rx_fifo_.front();
    rx_fifo_.pop_front();
    if (rx_valid_) {
      // El byte anterior seguía sin leerse y el CPU no puede ganar la carrera
      // al cable: aviso de overrun (como el receptor VHDL).
      rx_overrun_ = true;
    }
    rx_valid_ = true;
    update_irq();

    // La siguiente trama solo puede empezar cuando el CPU haya leído esta
    // (RX_VALID a 0); si no, el byte espera en la FIFO.
    start_next_frame();
  }
  if (rx_in_flight_) serial_countdown_ -= cycles;
}

void Uart::start_next_frame() {
  if (rx_in_flight_ || rx_valid_ || rx_fifo_.empty()) return;
  rx_in_flight_ = true;
  serial_countdown_ = BYTE_CYCLES(baud_div_);
}

void Uart::update_irq() {
  // El monitor usa polling de TX_READY para transmitir y no depende de la IRQ
  // de TX. En la emulación, mantener una IRQ de TX persistente haría que el
  // monitor reiniciara sin fin (su vector IRQ hace RTI + JMP RESET).
  // Por eso solo se modela la IRQ de recepción, que se limpia al leer $C020.
  irq_ = (rx_valid_ && rx_irq_en_);
}
