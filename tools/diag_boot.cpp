#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>

#include "bus.h"
#include "cpu_6502.h"
#include "ram_rom.h"
#include "sid.h"
#include "uart.h"

// Igual que main.cpp: drena el UART sólo cada 1000 instrucciones, y con SID.
int main() {
  std::ifstream f("rom/monitor.bin", std::ios::binary);
  std::vector<uint8_t> rom(16384);
  f.read((char*)rom.data(), 16384);

  Bus bus;
  bus.ram_rom()->load_rom(rom.data(), rom.size());
  Cpu6502 cpu(bus);
  cpu.reset();
  Uart& uart = *bus.uart();
  Sid& sid = *bus.sid();

  std::string out;
  int16_t abuf[1024];
  for (long it = 0; it < 3000; it++) {
    for (int i = 0; i < 1000; i++) cpu.step();
    while (uart.has_tx()) out.push_back((char)uart.dequeue_tx());
    sid.clock(292, abuf, 1024);
    if (it % 300 == 0)
      std::printf("it=%ld PC=$%04X bytes=%zu\n", it, cpu.pc(), out.size());
  }
  std::printf("--- total %zu bytes ---\n%s\n", out.size(), out.c_str());
  return 0;
}
