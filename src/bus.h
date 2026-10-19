#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "device.h"

// Conecta el CPU 6502 con todos los periféricos del sistema (RAM, ROM, vídeo,
// SID, SPI, GPIO, UART, Timer). Replica el Data_bus_mux del FPGA: la CPU pone
// una dirección y este bus decide qué dispositivo responde.
//
// Mapa de memoria (coincide con Data_bus_mux.vhd):
//   $0000-$3FFF  RAM 16 KB
//   $8000-$BFFF  ROM 16 KB
//   $C000-$C0FF  GPIO + I2C + UART + Timer + SPI
//   $D400-$D41F  SID 6581
//   $D800-$D817  Vídeo
//   $FFFA-$FFFF  Vectores -> redirigidos a ROM $3FFA-$3FFF
class Bus {
public:
  Bus();
  ~Bus();

  uint8_t read(uint16_t addr);
  void    write(uint16_t addr, uint8_t data);

  void set_irq(bool v) { irq_ = v; }
  void set_nmi(bool v) { nmi_ = v; }
  bool irq() const     { return irq_; }
  bool nmi() const     { return nmi_; }

  // Acceso a las piezas concretas que otros componentes necesitan.
  class RamRom*   ram_rom() { return ram_rom_; }
  class Video*    video()   { return video_; }
  class Sid*      sid()     { return sid_; }
  class SpiSd*    spi()     { return spi_; }
  class Gpio*     gpio()    { return gpio_; }
  class Timer*    timer()   { return timer_; }
  class Uart*     uart()    { return uart_; }

  // Línea IRQ combinada (activa en BAJO, como Board.vhd: irq_combined <= not ...)
  bool irq_asserted() const;

private:
  uint8_t read_rom_vector(uint16_t addr);

  std::vector<std::unique_ptr<Device>> devices_;

  class RamRom*   ram_rom_ = nullptr;
  class Video*    video_   = nullptr;
  class Sid*      sid_     = nullptr;
  class SpiSd*    spi_     = nullptr;
  class Gpio*     gpio_    = nullptr;
  class Timer*    timer_   = nullptr;
  class Uart*     uart_    = nullptr;

  bool irq_ = false;
  bool nmi_ = false;
};
