#include "timer.h"

Timer::Timer() {
  timer_ctl_ = 0x80; // ZERO = 1 (cuenta regresiva en 0)
}

bool Timer::decodes(uint16_t addr) const {
  return addr >= 0xC030 && addr <= 0xC03F;
}

uint8_t Timer::read(uint16_t addr, bool& resp) {
  resp = true;
  switch (addr) {
    // Contadores LEÍDOS POR LATCH (timer_rtc.vhd): se cargan escribiendo $C03C
    // con el bit 0 (ticks) o el bit 1 (us). Es lo que usa el SID player para
    // medir el frame: leer el contador vivo daría un valor incoherente entre
    // los 4 bytes y falsearía el ritmo.
    case 0xC030: return (uint8_t)(tick_latch_ & 0xFF);
    case 0xC031: return (uint8_t)((tick_latch_ >> 8) & 0xFF);
    case 0xC032: return (uint8_t)((tick_latch_ >> 16) & 0xFF);
    case 0xC033: return (uint8_t)((tick_latch_ >> 24) & 0xFF);
    case 0xC034: return (uint8_t)(timer_lo_ & 0xFF);
    case 0xC035: return (uint8_t)((timer_lo_ >> 8) & 0xFF);
    case 0xC036: return timer_ctl_;
    case 0xC037: return prescaler_;
    case 0xC038: return (uint8_t)(usec_latch_ & 0xFF);
    case 0xC039: return (uint8_t)((usec_latch_ >> 8) & 0xFF);
    case 0xC03A: return (uint8_t)((usec_latch_ >> 16) & 0xFF);
    case 0xC03B: return (uint8_t)((usec_latch_ >> 24) & 0xFF);
    case 0xC03C: return latch_ctl_;
    default: resp = false; return 0;
  }
}

void Timer::write(uint16_t addr, uint8_t data, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xC034: timer_lo_ = (timer_lo_ & 0xFF00) | data; break;
    case 0xC035: timer_lo_ = (timer_lo_ & 0x00FF) | (data << 8); break;
    case 0xC036: timer_ctl_ = data; break;
    case 0xC037: prescaler_ = data; break;
    case 0xC03C:
      latch_ctl_ = data;
      if (data & 0x01) tick_latch_ = ticks_;       // bit 0: latch TICK
      if (data & 0x02) usec_latch_ = usec_;        // bit 1: latch USEC
      if (data & 0x80) tick_latch_ = 0;            // bit 7: reset TICK
      // bit 6: reset del contador de microsegundos
      if (data & 0x40) {
        usec_ = 0;
      }
      break;
    default: resp = false; break;
  }
}

void Timer::tick(uint32_t cpu_cycles) {
  // timer_rtc.vhd corre con el reloj de sistema de 6.75 MHz y divide por
  // TICKS_PER_US = 7 para obtener microsegundos. La CPU de este emulador
  // avanza a 3.375 MHz (la mitad), así que 1 us = 3.5 ciclos de CPU.
  //
  // Se lleva un acumulador en CICLOS DE 6.75 MHz (2 por ciclo de CPU) para no
  // perder precisión con el redondeo: antes se truncaba en cada llamada y el
  // reloj se quedaba un 3.5 % corto, lo que ralentizaba la música.
  ticks_ += cpu_cycles * 2;

  // Microsegundos. 1 us = 3.5 ciclos de CPU. Para no perder fracción se acumula
  // en CICLOS DE CPU (multiplicados por 2 para trabajar con enteros), no en
  // microsegundos redondeados: redondear en cada llamada perdía ~3.5 %.
  usec_frac_ += cpu_cycles * 2;   // dobles ciclos de CPU
  const uint32_t us = usec_frac_ / 7; // 7 dobles-ciclos = 3.5 ciclos = 1 us
  usec_frac_ %= 7;
  usec_ += us;

  // Timer programable (solo si está habilitado).
  for (uint32_t i = 0; i < us; i++) {
    if ((timer_ctl_ & 0x01) && timer_lo_ > 0) { // EN y cuenta > 0
      timer_lo_--;
      if (timer_lo_ == 0) {
        timer_ctl_ |= 0x80;         // ZERO
        timer_ctl_ |= 0x08;         // IRQ_FLAG
        if (!(timer_ctl_ & 0x02)) { // !REPEAT -> apagar
          timer_ctl_ &= ~0x01;
        }
      }
    }
  }
}
