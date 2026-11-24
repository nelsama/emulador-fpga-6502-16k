#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "device.h"

// RAM 32 KB ($0000-$7FFF: 2 KB de BSRAM y 30 KB de PSRAM, sin diferencias para
// el programa) y ROM 16 KB ($8000-$BFFF) + vectores.
class RamRom : public Device {
public:
  static constexpr unsigned RAM_BYTES = 0x8000;

  RamRom();

  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Carga el contenido de la ROM del monitor desde un binario de 16 KB.
  void load_rom(const std::array<uint8_t, 16384>& rom);
  void load_rom(const uint8_t* rom, size_t n);

  const uint8_t* ram() const { return ram_.data(); }
  uint8_t* ram() { return ram_.data(); }

private:
  std::array<uint8_t, RAM_BYTES> ram_{};
  std::array<uint8_t, 16384> rom_{};
};
