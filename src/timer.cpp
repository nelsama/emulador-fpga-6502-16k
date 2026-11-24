#include "timer.h"

// Réplica de timer_rtc.vhd. Todo corre con el reloj de sistema de 6,75 MHz: 2 pulsos
// por ciclo de CPU.

bool Timer::decodes(uint16_t addr) const {
  return addr >= 0xC030 && addr <= 0xC03F;
}

uint8_t Timer::read(uint16_t addr, bool& resp) {
  resp = decodes(addr);
  if (!resp) return 0;
  switch (addr & 0x0F) {
    // Contadores LEÍDOS POR LATCH: se capturan escribiendo $C03C.
    case 0x0: return (uint8_t)tick_latch_;
    case 0x1: return (uint8_t)(tick_latch_ >> 8);
    case 0x2: return (uint8_t)(tick_latch_ >> 16);
    case 0x3: return (uint8_t)(tick_latch_ >> 24);
    case 0x4: return (uint8_t)valor_;
    case 0x5: return (uint8_t)(valor_ >> 8);
    case 0x6:
      return (uint8_t)((zero_ ? 0x80 : 0) | (irq_flag_ ? 0x08 : 0) | (repeat_ ? 0x04 : 0) |
                       (irq_en_ ? 0x02 : 0) | (en_ ? 0x01 : 0));
    case 0x7: return prescaler_;
    case 0x8: return (uint8_t)usec_latch_;
    case 0x9: return (uint8_t)(usec_latch_ >> 8);
    case 0xA: return (uint8_t)(usec_latch_ >> 16);
    case 0xB: return (uint8_t)(usec_latch_ >> 24);
    default:  return 0;
  }
}

void Timer::write(uint16_t addr, uint8_t data, bool& resp) {
  resp = decodes(addr);
  if (!resp) return;
  switch (addr & 0x0F) {
    case 0x4:
      recarga_ = (uint16_t)((recarga_ & 0xFF00) | data);
      valor_ = (uint16_t)((valor_ & 0xFF00) | data);
      break;
    case 0x5:
      recarga_ = (uint16_t)((recarga_ & 0x00FF) | (data << 8));
      valor_ = (uint16_t)((valor_ & 0x00FF) | (data << 8));
      break;
    case 0x6:
      en_ = (data & 0x01) != 0;
      irq_en_ = (data & 0x02) != 0;
      repeat_ = (data & 0x04) != 0;
      if (data & 0x08) irq_flag_ = false; // se borran escribiendo 1
      if (data & 0x80) zero_ = false;
      break;
    case 0x7:
      prescaler_ = data;
      presc_cnt_ = 0;
      break;
    case 0xC:
      if (data & 0x01) tick_latch_ = ticks_;
      if (data & 0x02) usec_latch_ = usec_;
      if (data & 0x80) tick_latch_ = 0;
      if (data & 0x40) {
        usec_ = 0;
        usec_presc_ = 0;
      }
      break;
    default: break;
  }
}

void Timer::reset() { *this = Timer(); }

void Timer::tick(uint32_t cpu_cycles) {
  const uint32_t n = cpu_cycles * 2;
  ticks_ += n;

  usec_presc_ += n;               // TICKS_PER_US = 7
  usec_ += usec_presc_ / 7;
  usec_presc_ %= 7;

  for (uint32_t i = 0; i < n && en_; i++) {
    if (presc_cnt_ != prescaler_) {
      presc_cnt_++;
      continue;
    }
    presc_cnt_ = 0;
    if (valor_ != 0) {
      valor_--;
      continue;
    }
    zero_ = true;
    irq_flag_ = true;
    if (repeat_) valor_ = recarga_;
    else en_ = false;
  }
}
