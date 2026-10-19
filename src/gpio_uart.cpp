#include "gpio_uart.h"

bool Gpio::decodes(uint16_t addr) const {
  return addr >= 0xC000 && addr <= 0xC003;
}

uint8_t Gpio::read(uint16_t addr, bool& resp) {
  resp = true;
  switch (addr) {
    // Puertos bidireccionales: cada bit devuelve los pines si está configurado
    // como entrada, o el registro de salida si está como salida (Board.vhd).
    case 0xC000: return port1_read();
    case 0xC001: return port2_read();
    case 0xC002: return dir1_;
    case 0xC003: return dir2_;
    default: resp = false; return 0;
  }
}

void Gpio::write(uint16_t addr, uint8_t data, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xC000: port1_out_ = data; break;
    case 0xC001:
      // En el hardware el pin y el registro son lo mismo cuando está en salida;
      // se guarda el registro y se combina con los pines al leer (los LEDs no
      // se leen de vuelta, así que port2_ se queda en 1s).
      port2_out_ = data;
      break;
    case 0xC002: dir1_ = data; break;
    case 0xC003: dir2_ = data; break;
    default: resp = false; break;
  }
}
