// Diagnóstico del bucle de entrada del monitor (rutina getc en $80D3).
//
// Uso: drx.exe            -> arranca y envía "H\r" byte a byte (87 us)
//      drx.exe <texto>    -> envía ese texto (se soportan \r y \n)
//
// Simula la llegada serial real: cada byte del host se entrega al UART y solo
// se publica al CPU tras ~87 us (baudios), llamando a Uart::tick() como hace
// main.cpp. Todo lo que no sea ">" del aviso de reset se toma como salida del
// monitor.
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "bus.h"
#include "cpu_6502.h"
#include "ram_rom.h"
#include "sd_card.h"
#include "uart.h"

static const uint32_t CPU_HZ = 3375000;
static const uint32_t BYTE_CYCLES = 10 * (6750000 / 115200); // ~586 ciclos

static std::string out;

static void run(Cpu6502& cpu, Uart& u, long cycles) {
  for (long i = 0; i < cycles; i++) {
    cpu.step();
    u.tick(1);
    while (u.has_tx()) out.push_back((char)u.dequeue_tx());
  }
}

int main(int argc, char** argv) {
  std::string input = argc > 1 ? argv[1] : "H\r";
  std::string seq;
  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      char c = input[++i];
      seq.push_back(c == 'r' ? '\r' : c == 'n' ? '\n' : c);
    } else {
      seq.push_back(input[i]);
    }
  }

  std::ifstream f("rom/monitor.bin", std::ios::binary);
  std::vector<uint8_t> rom(16384);
  f.read((char*)rom.data(), 16384);

  Bus bus;
  bus.ram_rom()->load_rom(rom.data(), rom.size());
  SdCardImpl card;
  card.open(".tmp/drx2.img", 16);
  bus.spi()->attach(&card);

  Cpu6502 cpu(bus);
  cpu.reset();
  Uart& u = *bus.uart();

  // Arranque: esperar el prompt del monitor.
  for (int i = 0; i < 2000 && (out.empty() || out.back() != '>'); i++) run(cpu, u, 5000);

  // Ojo: el monitor termina en CLI; el emulador debe soportar IRQ sin RTI
  // (el vector IRQ de la ROM es un JMP a $8000, el manejador por defecto).
  out.clear();

  for (char c : seq) {
    u.enqueue_rx((uint8_t)c);
    run(cpu, u, BYTE_CYCLES);
  }
  run(cpu, u, CPU_HZ / 10); // 100 ms para que el monitor termine de responder

  std::printf("Entrada: \"%s\"\nSalida:", input.c_str());
  for (unsigned char c : out) {
    if (c == '\r') std::printf("\\r");
    else if (c == '\n') std::printf("\\n\n        ");
    else std::printf("%c", c);
  }
  std::printf("\n");
  return 0;
}
