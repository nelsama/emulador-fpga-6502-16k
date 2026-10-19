#pragma once

#include <cstdint>

// Interfaz de un periférico del bus 6502. Lee/escribe un byte en una dirección
// concreta sólo si esa dirección le corresponde (resp = true).
struct Device {
  virtual ~Device() = default;

  // Devuelve true si este dispositivo atiende la dirección addr.
  virtual bool decodes(uint16_t addr) const = 0;

  // Lee un byte. Devuelve el dato y, mediante resp, si lo atendió.
  virtual uint8_t read(uint16_t addr, bool& resp) = 0;

  // Escribe un byte. resp == true si este dispositivo lo consumió.
  virtual void write(uint16_t addr, uint8_t data, bool& resp) = 0;
};
