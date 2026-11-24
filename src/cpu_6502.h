#pragma once

#include <cstdint>

class Bus;

// CPU 6502 (NMOS/6510), con el modelo del cpu65xx_fast.vhd de Peter Wendrich
// (FPGA64): opcodes ilegales, modo decimal y ciclos exactos por instrucción. Expone:
//   reset(): pone PC en el vector de RESET ($FFFC-$FFFD).
//   step():  ejecuta una instrucción.
//   irq/nmi: líneas de interrupción.
class Cpu6502 {
public:
  Cpu6502(Bus& bus);

  void reset();
  void step();
  void irq();
  void nmi();

  // Estado para el debugger.
  uint16_t pc() const { return pc_; }
  uint8_t  a()  const { return a_; }
  uint8_t  x()  const { return x_; }
  uint8_t  y()  const { return y_; }
  uint8_t  sp() const { return sp_; }
  uint8_t  sr() const { return sr_; }
  bool     parada() const { return jam_; }

  // Ciclos consumidos por la última instrucción ejecutada (2-8).
  int last_cycles() const { return last_cycles_; }

  // Ciclos totales desde el arranque (solo para diagnóstico). Sirve para medir
  // cuánto tarda una rutina a 3.375 MHz sin instrumentar el programa.
  uint64_t ciclos_totales() const { return ciclos_totales_; }

  // Reset desactiva el contador: los ciclos se refieren a la ejecución actual.
  void reiniciar_ciclos() { ciclos_totales_ = 0; }

private:
  // Registros
  uint16_t pc_ = 0;
  uint8_t  a_ = 0, x_ = 0, y_ = 0, sp_ = 0;
  uint8_t  sr_ = 0; // N V - B D I Z C
  bool     i_muestreado_ = true; // flag I tal como la ve el muestreo de IRQ
  bool     jam_ = false;         // CPU parada por un opcode JAM
  int      last_cycles_ = 2;
  uint64_t ciclos_totales_ = 0;

  Bus& bus_;

  // Helpers de bus
  uint8_t  read(uint16_t addr);
  void     write(uint16_t addr, uint8_t v);
  uint16_t read16(uint16_t addr);

  void     push(uint8_t v);
  uint8_t  pop();
  void     interrumpir(uint16_t vector, uint8_t flags);

  // Flags y ALU
  void set_flag(uint8_t f, bool on);
  void set_z_n(uint8_t v);
  void adc(uint8_t m, bool c);
  void sbc(uint8_t m, bool c);
  void cmp(uint8_t reg, uint8_t m);
  uint8_t asl(uint8_t v, bool c);
  uint8_t lsr(uint8_t v, bool c);
  void arr(uint8_t m);
};
