#pragma once

#include <cstdint>

#include "device.h"

// Timer / RTC ($C030-$C03F), réplica de timer_rtc.vhd.
// Ticks, cuenta atrás con prescaler, recarga e IRQ, microsegundos y latch.
class Timer : public Device {
public:
  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Avanza el timer con los ciclos de CPU transcurridos (3.375 MHz).
  void tick(uint32_t cpu_cycles);

  // Línea IRQ (activa alta).
  bool irq() const { return irq_flag_ && irq_en_; }

  // Botón de reset de la placa.
  void reset();

private:
  uint32_t ticks_ = 0;      // pulsos de 6,75 MHz
  uint32_t tick_latch_ = 0; // $C030-$C033
  uint32_t usec_ = 0;
  uint32_t usec_latch_ = 0; // $C038-$C03B
  uint32_t usec_presc_ = 0;

  uint16_t valor_ = 0;      // $C034-$C035
  uint16_t recarga_ = 0;
  uint8_t  prescaler_ = 0;  // $C037: divide por prescaler+1
  uint8_t  presc_cnt_ = 0;
  // $C036
  bool en_ = false;         // bit 0
  bool irq_en_ = false;     // bit 1
  bool repeat_ = false;     // bit 2
  bool irq_flag_ = false;   // bit 3 (escribir 1 lo borra)
  bool zero_ = false;       // bit 7 (escribir 1 lo borra)
};
