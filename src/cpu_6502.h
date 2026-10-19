#pragma once

#include <cstdint>

class Bus;

// CPU 6502 ciclo-aproximado.
//
// En esta primera versión usamos una implementación basada en el modelo del
// cpu65xx_fast.vhd de Peter Wendrich (FPGA64). Expone:
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

  // Ciclos consumidos por la última instrucción ejecutada (2-7 en el 6502).
  int last_cycles() const { return last_cycles_; }

private:
  // Registros
  uint16_t pc_ = 0;
  uint8_t  a_ = 0, x_ = 0, y_ = 0, sp_ = 0;
  uint8_t  sr_ = 0; // N V - B D I Z C
  int      last_cycles_ = 2;

  Bus& bus_;

  // Helpers de bus
  uint8_t  read(uint16_t addr);
  void     write(uint16_t addr, uint8_t v);
  uint16_t read16(uint16_t addr);

  // Flags
  void set_z_n(uint8_t v);
  void set_carry(bool c);
  // etc. (se completan con la implementación)
};
