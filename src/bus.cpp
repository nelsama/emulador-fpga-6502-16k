#include "bus.h"

#include "gpio_uart.h"
#include "ram_rom.h"
#include "sid.h"
#include "spi_sd.h"
#include "timer.h"
#include "uart.h"
#include "video/video.h"

Bus::Bus() {
  auto ram = std::make_unique<RamRom>();
  auto vid = std::make_unique<Video>();
  auto s   = std::make_unique<Sid>();
  auto sd  = std::make_unique<SpiSd>();
  auto gp  = std::make_unique<Gpio>();
  auto tm  = std::make_unique<Timer>();
  auto ua  = std::make_unique<Uart>();

  ram_rom_ = ram.get();
  video_   = vid.get();
  sid_     = s.get();
  spi_     = sd.get();
  gpio_    = gp.get();
  timer_   = tm.get();
  uart_    = ua.get();

  devices_.push_back(std::move(ram));
  devices_.push_back(std::move(vid));
  devices_.push_back(std::move(s));
  devices_.push_back(std::move(sd));
  devices_.push_back(std::move(gp));
  devices_.push_back(std::move(tm));
  devices_.push_back(std::move(ua));
}

Bus::~Bus() = default;

uint8_t Bus::read(uint16_t addr) {
  if (addr >= 0xFFFA) {
    return read_rom_vector(addr);
  }
  for (auto& d : devices_) {
    bool resp = false;
    uint8_t v = d->read(addr, resp);
    if (resp) return v;
  }
  return 0xFF;
}

void Bus::write(uint16_t addr, uint8_t data) {
  for (auto& d : devices_) {
    bool resp = false;
    d->write(addr, data, resp);
    if (resp) return;
  }
}

uint8_t Bus::read_rom_vector(uint16_t addr) {
  // $FFFA-$FFFF mapean a ROM física $BFFA-$BFFF ($8000 + $3FFA + idx).
  uint16_t rom_addr = 0x8000 + 0x3FFA + (addr - 0xFFFA);
  bool resp = false;
  return ram_rom_->read(rom_addr, resp);
}

bool Bus::irq_asserted() const {
  // Replica: irq_combined <= not (i2c_irq or uart_irq or timer_irq or spi_irq)
  // En el VHDL cada *_irq es '1' = inactivo, '0' = IRQ pedida. Aquí uart.irq()
  // devuelve true cuando pide interrupción, así que:
  //   irq_asserted (línea baja = pide IRQ)  <=>  algún módulo pide IRQ
  bool any = uart_ ? uart_->irq() : false;
  return any;
}
