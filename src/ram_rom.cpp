#include "ram_rom.h"

#include <cstring>

RamRom::RamRom() {
  rom_.fill(0xFF); // como "when others => xFF" del VHDL
}

bool RamRom::decodes(uint16_t addr) const {
  if (addr < 0x4000) return true;            // RAM
  if (addr >= 0x8000 && addr < 0xC000) return true; // ROM
  return false;
}

uint8_t RamRom::read(uint16_t addr, bool& resp) {
  resp = true;
  if (addr < 0x4000) return ram_[addr];
  if (addr >= 0x8000 && addr < 0xC000) return rom_[addr - 0x8000];
  resp = false;
  return 0;
}

void RamRom::write(uint16_t addr, uint8_t data, bool& resp) {
  if (addr < 0x4000) {
    ram_[addr] = data;
    resp = true;
    return;
  }
  // La ROM no es escribible en esta versión.
  resp = false;
}

void RamRom::load_rom(const std::array<uint8_t, 16384>& rom) {
  rom_ = rom;
}

void RamRom::load_rom(const uint8_t* rom, size_t n) {
  size_t m = n > 16384 ? 16384 : n;
  std::memcpy(rom_.data(), rom, m);
}
