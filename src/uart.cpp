#include "uart.h"

// Ciclos de CPU que tarda una trama 8N1 (10 bits a 115200 baudios).
//
// El divisor del UART cuenta ciclos de 6.75 MHz (uart.vhd: BAUD_RATE=>115200,
// CLK_FREQ=>6_750_000 -> 58 ciclos por bit) y la CPU anda a 3.375 MHz, es
// decir, la mitad. Así que una trama son 10 x 58 / 2 = 5 x 58 = 290 ciclos de CPU.
static inline uint32_t BYTE_CYCLES(uint16_t divisor) {
  return divisor ? 5u * divisor : 1u;
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
      return rx_buffer_;
    case 0xC021: // STATUS
      {
        uint8_t s = (uint8_t)(
          (rx_overrun_ ? 0x10 : 0x00) |
          (rx_error_   ? 0x08 : 0x00) |
          (tx_ready_   ? 0x00 : 0x04) | // TX_BUSY
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
      // La trama dura 10 bits (~87 us) y mientras tanto TX_READY = 0. Escribir
      // antes de que acabe deja el byte esperando (uart.vhd: tx_trigger).
      if (tx_ready_) {
        iniciar_tx(data);
      } else {
        tx_dato_ = data;
        tx_pendiente_ = true;
      }
      break;
    case 0xC021: // CONTROL
      tx_irq_en_ = (data & 0x01) != 0;
      rx_irq_en_ = (data & 0x02) != 0;
      if (data & 0x80) { // RESET_FLAGS
        rx_error_ = false;
        rx_overrun_ = false;
      }
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

void Uart::iniciar_tx(uint8_t v) {
  tx_ready_ = false;
  tx_countdown_ = BYTE_CYCLES(baud_div_);
  // Sin nadie conectado se conserva lo MÁS RECIENTE.
  if (tx_fifo_.size() >= TX_FIFO_MAX) tx_fifo_.pop_front();
  tx_fifo_.push_back(v);
  // Copia para el terminal de la ventana. No consume la FIFO.
  tap_push(v);
}

uint8_t Uart::dequeue_tx() {
  uint8_t v = tx_fifo_.front();
  tx_fifo_.pop_front();
  return v;
}

void Uart::tap_push(uint8_t v) {
  // Cola para quien consuma byte a byte (si se quiere). Se descarta lo mas
  // antiguo si nadie la vacia, para no crecer sin limite.
  if (tap_.size() >= TAP_MAX) tap_.pop_front();
  tap_.push_back(v);

  // El terminal interpreta el flujo: ANSI + CP437. Ver terminal_state.h.
  term_.feed(v);
  version_term_++;
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
  if (!tx_ready_) {
    if (cycles < tx_countdown_) {
      tx_countdown_ -= cycles;
    } else {
      tx_ready_ = true;
      if (tx_pendiente_) {
        tx_pendiente_ = false;
        iniciar_tx(tx_dato_);
      }
    }
  }
  while (rx_in_flight_ && cycles >= serial_countdown_) {
    cycles -= serial_countdown_;

    // Fin de trama: el byte queda disponible en RX_BUFFER.
    rx_in_flight_ = false;
    if (rx_fifo_.empty()) break; // defensivo
    rx_buffer_ = rx_fifo_.front();
    rx_fifo_.pop_front();
    rx_valid_ = true;

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
