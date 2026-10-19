#pragma once

#include <cstdint>

#include "device.h"

// Timer de precisión / RTC ($C030-$C03F).
// Ticks, countdown, control, prescaler, microsegundos y latch.
class Timer : public Device {
public:
  Timer();

  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Avanza el timer con los ciclos de CPU transcurridos (3.375 MHz). El
  // contador de microsegundos corre siempre, como en timer_rtc.vhd.
  void tick(uint32_t cpu_cycles);

private:
  uint32_t ticks_ = 0;      // $C030-$C033 (por latch)
  uint32_t tick_latch_ = 0; // copia estable de ticks_
  uint16_t timer_lo_ = 0;   // $C034-$C035 countdown
  uint8_t  timer_ctl_ = 0;  // $C036 : EN(0) IRQ_EN(1) REPEAT(2) IRQ_FLAG(3) ZERO(7)
  uint8_t  prescaler_ = 0;  // $C037
  uint32_t usec_ = 0;       // $C038-$C03B (por latch)
  uint32_t usec_latch_ = 0;
  uint32_t usec_frac_ = 0;  // resto en ticks de 6.75 MHz (1/7 de us)
  uint8_t  latch_ctl_ = 0;  // $C03C
};
