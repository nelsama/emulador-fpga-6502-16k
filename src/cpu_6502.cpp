#include "cpu_6502.h"

#include <cstdio>

#include "bus.h"
#include "cpu_cycles.h"
#include "cpu_opcodes.h"

// Réplica funcional de cpu65xx_fast.vhd (6510 NMOS, con opcodes ilegales y modo
// decimal). Los ciclos son los del núcleo, incluidos los de rama tomada y cruce de
// página. Las instrucciones de lectura-modificación-escritura solo escriben el
// valor final: los periféricos del FPGA detectan la escritura por flanco, así que
// la doble escritura del 6502 cuenta como una.

namespace {
constexpr uint8_t FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08, FB = 0x10, F1 = 0x20,
                  FV = 0x40, FN = 0x80;
}

Cpu6502::Cpu6502(Bus& bus) : bus_(bus) {}

uint8_t Cpu6502::read(uint16_t addr) { return bus_.read(addr); }
void    Cpu6502::write(uint16_t addr, uint8_t v) { bus_.write(addr, v); }

uint16_t Cpu6502::read16(uint16_t addr) {
  const uint8_t lo = read(addr);
  return (uint16_t)(lo | (read((uint16_t)(addr + 1)) << 8));
}

void Cpu6502::push(uint8_t v) { write((uint16_t)(0x0100 | sp_--), v); }
uint8_t Cpu6502::pop() { return read((uint16_t)(0x0100 | ++sp_)); }

void Cpu6502::set_flag(uint8_t f, bool on) {
  if (on) sr_ |= f; else sr_ &= (uint8_t)~f;
}

void Cpu6502::set_z_n(uint8_t v) {
  set_flag(FZ, v == 0);
  set_flag(FN, (v & 0x80) != 0);
}

void Cpu6502::reset() {
  // El vector de RESET está en $FFFC-$FFFD (que mapea a ROM $BFFC-$BFFD).
  pc_ = read16(0xFFFC);
  sp_ = 0xFD;
  sr_ = F1 | FI;
  i_muestreado_ = true;
  jam_ = false;
  a_ = x_ = y_ = 0;
  ciclos_totales_ = 0;
}

void Cpu6502::interrumpir(uint16_t vector, uint8_t flags) {
  push((uint8_t)(pc_ >> 8));
  push((uint8_t)pc_);
  push(flags);
  sr_ |= FI;
  i_muestreado_ = true;
  pc_ = read16(vector);
}

void Cpu6502::irq() {
  if (sr_ & FI) return;
  interrumpir(0xFFFE, (uint8_t)((sr_ & ~FB) | F1));
}

void Cpu6502::nmi() { interrumpir(0xFFFA, (uint8_t)((sr_ & ~FB) | F1)); }

// --- ALU --------------------------------------------------------------------

// Suma en decimal como el VHDL: Z sale del resultado binario; N y V, del resultado
// con el nibble bajo ya corregido; C, de la corrección del nibble alto.
void Cpu6502::adc(uint8_t m, bool c) {
  unsigned r = a_ + m + (c ? 1 : 0);
  const unsigned bajo = (a_ & 0x0F) + (m & 0x0F) + (c ? 1 : 0);
  set_flag(FZ, (r & 0xFF) == 0);
  unsigned alto = r >> 4;       // 5 bits
  unsigned lo = r & 0x0F;
  if ((sr_ & FD) && bajo > 9) {
    lo = (lo + 6) & 0x0F;
    if (bajo < 16) alto = (alto + 1) & 0x1F;
  }
  const uint8_t medio = (uint8_t)((alto << 4) | lo);
  set_flag(FN, (medio & 0x80) != 0);
  set_flag(FV, ((a_ ^ medio) & (m ^ medio) & 0x80) != 0);
  bool carry = (alto & 0x10) != 0;
  if ((sr_ & FD) && alto > 9) {
    alto += 6;
    carry = true;
  }
  set_flag(FC, carry);
  a_ = (uint8_t)((alto << 4) | lo);
}

// Resta: todos los flags salen del resultado binario; en decimal solo se corrige A.
void Cpu6502::sbc(uint8_t m, bool c) {
  const uint8_t im = (uint8_t)~m;
  const unsigned r = a_ + im + (c ? 1 : 0);
  const unsigned bajo = (a_ & 0x0F) + (im & 0x0F) + (c ? 1 : 0);
  const uint8_t bin = (uint8_t)r;
  set_z_n(bin);
  set_flag(FV, ((a_ ^ bin) & (im ^ bin) & 0x80) != 0);
  set_flag(FC, r > 0xFF);
  unsigned alto = r >> 4, lo = r & 0x0F;
  if (sr_ & FD) {
    if (bajo < 16) lo -= 6;
    if (r <= 0xFF) alto -= 6;
  }
  a_ = (uint8_t)(((alto & 0x0F) << 4) | (lo & 0x0F));
}

void Cpu6502::cmp(uint8_t reg, uint8_t m) {
  set_z_n((uint8_t)(reg - m));
  set_flag(FC, reg >= m);
}

uint8_t Cpu6502::asl(uint8_t v, bool c) {
  set_flag(FC, (v & 0x80) != 0);
  v = (uint8_t)((v << 1) | (c ? 1 : 0));
  set_z_n(v);
  return v;
}

uint8_t Cpu6502::lsr(uint8_t v, bool c) {
  set_flag(FC, (v & 0x01) != 0);
  v = (uint8_t)((v >> 1) | (c ? 0x80 : 0));
  set_z_n(v);
  return v;
}

void Cpu6502::arr(uint8_t m) {
  const uint8_t t = a_ & m;
  uint8_t r = (uint8_t)((t >> 1) | ((sr_ & FC) ? 0x80 : 0));
  set_z_n(r);
  set_flag(FC, (t & 0x80) != 0);
  set_flag(FV, (((t >> 7) ^ (t >> 6)) & 1) != 0);
  if (sr_ & FD) {
    if ((t & 0x0F) + (t & 0x01) > 5) r = (uint8_t)((r & 0xF0) | ((r + 6) & 0x0F));
    const bool ajuste = (t >> 4) + ((t >> 4) & 1) > 5;
    if (ajuste) r = (uint8_t)(r + 0x60);
    set_flag(FC, ajuste);
  }
  a_ = r;
}

// --- Ejecución --------------------------------------------------------------

void Cpu6502::step() {
  // El núcleo muestrea la IRQ antes de que CLI/SEI/PLP actualicen I, así que su
  // efecto llega una instrucción después (i_muestreado_).
  if (!i_muestreado_ && bus_.irq_asserted()) {
    interrumpir(0xFFFE, (uint8_t)((sr_ & ~FB) | F1));
    last_cycles_ = 7;
    ciclos_totales_ += 7;
    return;
  }
  if (jam_) { // parada: solo sale con un reset
    last_cycles_ = 2;
    ciclos_totales_ += 2;
    return;
  }

  const uint8_t opcode = read(pc_++);
  const Opcode o = kOpcodes[opcode];
  last_cycles_ = kOpcodeCycles[opcode];
  const uint8_t i_previo = sr_ & FI;

  // Dirección efectiva.
  uint16_t ea = 0, base = 0;
  switch (o.modo) {
    case IMP: case ACC: break;
    case IMM: case REL: ea = pc_++; break;
    case ZP:  ea = read(pc_++); break;
    case ZPX: ea = (uint8_t)(read(pc_++) + x_); break;
    case ZPY: ea = (uint8_t)(read(pc_++) + y_); break;
    case ABS: ea = read16(pc_); pc_ += 2; break;
    case ABX: base = read16(pc_); pc_ += 2; ea = (uint16_t)(base + x_); break;
    case ABY: base = read16(pc_); pc_ += 2; ea = (uint16_t)(base + y_); break;
    case IZX: {
      const uint8_t z = (uint8_t)(read(pc_++) + x_);
      ea = (uint16_t)(read(z) | (read((uint8_t)(z + 1)) << 8));
      break;
    }
    case IZY: {
      const uint8_t z = read(pc_++);
      base = (uint16_t)(read(z) | (read((uint8_t)(z + 1)) << 8));
      ea = (uint16_t)(base + y_);
      break;
    }
    case IND: {
      const uint16_t p = read16(pc_);
      pc_ += 2;
      // El 6510 (y el VHDL) no pasa de página al leer el byte alto.
      ea = (uint16_t)(read(p) |
                      (read((uint16_t)((p & 0xFF00) | (uint8_t)(p + 1))) << 8));
      break;
    }
  }
  const bool indexado = o.modo == ABX || o.modo == ABY || o.modo == IZY;
  const bool cruza = indexado && ((base ^ ea) & 0xFF00);

  // Operando de una instrucción de LECTURA: +1 ciclo si cruza página.
  auto leer = [&]() -> uint8_t {
    if (cruza) last_cycles_++;
    return read(ea);
  };
  auto rama = [&](bool tomada) {
    const int8_t d = (int8_t)read(ea);
    if (!tomada) return;
    const uint16_t destino = (uint16_t)(pc_ + d);
    last_cycles_ += ((destino ^ pc_) & 0xFF00) ? 2 : 1;
    pc_ = destino;
  };
  auto cargar = [&](uint8_t& reg, uint8_t v) { reg = v; set_z_n(v); };
  const bool c = (sr_ & FC) != 0;
  // SHA/SHX/SHY/SHS: el valor se enmascara con el byte alto de la base + 1.
  const uint8_t h1 = (uint8_t)((base >> 8) + 1);

  switch (o.op) {
    // Cargas y almacenamientos
    case LDA: cargar(a_, leer()); break;
    case LDX: cargar(x_, leer()); break;
    case LDY: cargar(y_, leer()); break;
    case LAX: cargar(a_, leer()); x_ = a_; break;
    case LXA: cargar(a_, (uint8_t)((a_ | 0xEE) & leer())); x_ = a_; break;
    case LAS: cargar(a_, (uint8_t)(sp_ & leer())); x_ = sp_ = a_; break;
    case STA: write(ea, a_); break;
    case STX: write(ea, x_); break;
    case STY: write(ea, y_); break;
    case SAX: write(ea, a_ & x_); break;
    case SHA: write(ea, a_ & x_ & h1); break;
    case SHX: write(ea, x_ & h1); break;
    case SHY: write(ea, y_ & h1); break;
    case SHS: sp_ = a_ & x_; write(ea, sp_ & h1); break;

    // Transferencias y pila
    case TAX: cargar(x_, a_); break;
    case TAY: cargar(y_, a_); break;
    case TXA: cargar(a_, x_); break;
    case TYA: cargar(a_, y_); break;
    case TSX: cargar(x_, sp_); break;
    case TXS: sp_ = x_; break;
    case PHA: push(a_); break;
    case PHP: push((uint8_t)(sr_ | FB | F1)); break;
    case PLA: cargar(a_, pop()); break;
    case PLP: sr_ = (uint8_t)((pop() & ~FB) | F1); break;

    // Lógica y aritmética
    case ORA: cargar(a_, (uint8_t)(a_ | leer())); break;
    case AND: cargar(a_, (uint8_t)(a_ & leer())); break;
    case EOR: cargar(a_, (uint8_t)(a_ ^ leer())); break;
    case ADC: adc(leer(), c); break;
    case SBC: sbc(leer(), c); break;
    case CMP: cmp(a_, leer()); break;
    case CPX: cmp(x_, leer()); break;
    case CPY: cmp(y_, leer()); break;
    case BIT: {
      const uint8_t m = leer();
      set_flag(FZ, (a_ & m) == 0);
      set_flag(FN, (m & 0x80) != 0);
      set_flag(FV, (m & 0x40) != 0);
      break;
    }
    case ANC: cargar(a_, (uint8_t)(a_ & leer())); set_flag(FC, (a_ & 0x80) != 0); break;
    case ALR: a_ = lsr((uint8_t)(a_ & leer()), false); break;
    case ARR: arr(leer()); break;
    case ANE: cargar(a_, (uint8_t)((a_ | 0xEE) & x_ & leer())); break;
    case SBX: {
      const uint8_t m = leer(), ax = a_ & x_;
      cmp(ax, m);
      x_ = (uint8_t)(ax - m);
      break;
    }

    // Incrementos
    case INX: cargar(x_, (uint8_t)(x_ + 1)); break;
    case INY: cargar(y_, (uint8_t)(y_ + 1)); break;
    case DEX: cargar(x_, (uint8_t)(x_ - 1)); break;
    case DEY: cargar(y_, (uint8_t)(y_ - 1)); break;
    case INC: { const uint8_t v = (uint8_t)(read(ea) + 1); write(ea, v); set_z_n(v); break; }
    case DEC: { const uint8_t v = (uint8_t)(read(ea) - 1); write(ea, v); set_z_n(v); break; }

    // Desplazamientos
    case ASL: if (o.modo == ACC) a_ = asl(a_, false); else write(ea, asl(read(ea), false)); break;
    case ROL: if (o.modo == ACC) a_ = asl(a_, c);     else write(ea, asl(read(ea), c)); break;
    case LSR: if (o.modo == ACC) a_ = lsr(a_, false); else write(ea, lsr(read(ea), false)); break;
    case ROR: if (o.modo == ACC) a_ = lsr(a_, c);     else write(ea, lsr(read(ea), c)); break;

    // Combinadas (ilegales)
    case SLO: { const uint8_t v = asl(read(ea), false); write(ea, v); cargar(a_, (uint8_t)(a_ | v)); break; }
    case RLA: { const uint8_t v = asl(read(ea), c);     write(ea, v); cargar(a_, (uint8_t)(a_ & v)); break; }
    case SRE: { const uint8_t v = lsr(read(ea), false); write(ea, v); cargar(a_, (uint8_t)(a_ ^ v)); break; }
    case RRA: { const uint8_t v = lsr(read(ea), c);     write(ea, v); adc(v, (sr_ & FC) != 0); break; }
    case DCP: { const uint8_t v = (uint8_t)(read(ea) - 1); write(ea, v); cmp(a_, v); break; }
    case ISC: { const uint8_t v = (uint8_t)(read(ea) + 1); write(ea, v); sbc(v, c); break; }

    // Saltos
    case JMP: pc_ = ea; break;
    case JSR: {
      const uint16_t ret = (uint16_t)(pc_ - 1);
      push((uint8_t)(ret >> 8));
      push((uint8_t)ret);
      pc_ = ea;
      break;
    }
    case RTS: { const uint8_t lo = pop(); pc_ = (uint16_t)((lo | (pop() << 8)) + 1); break; }
    case RTI: {
      sr_ = (uint8_t)((pop() & ~FB) | F1);
      const uint8_t lo = pop();
      pc_ = (uint16_t)(lo | (pop() << 8));
      break;
    }
    case BRK: pc_++; interrumpir(0xFFFE, (uint8_t)(sr_ | FB | F1)); break;
    case BPL: rama(!(sr_ & FN)); break;
    case BMI: rama((sr_ & FN) != 0); break;
    case BVC: rama(!(sr_ & FV)); break;
    case BVS: rama((sr_ & FV) != 0); break;
    case BCC: rama(!(sr_ & FC)); break;
    case BCS: rama((sr_ & FC) != 0); break;
    case BNE: rama(!(sr_ & FZ)); break;
    case BEQ: rama((sr_ & FZ) != 0); break;

    // Flags
    case CLC: sr_ &= (uint8_t)~FC; break;
    case SEC: sr_ |= FC; break;
    case CLI: sr_ &= (uint8_t)~FI; break;
    case SEI: sr_ |= FI; break;
    case CLV: sr_ &= (uint8_t)~FV; break;
    case CLD: sr_ &= (uint8_t)~FD; break;
    case SED: sr_ |= FD; break;

    case NOP: if (o.modo != IMP) (void)leer(); break;
    case JAM:
      // En el VHDL el comportamiento es indefinido; un 6502 real se detiene.
      jam_ = true;
      pc_--;
      std::fprintf(stderr, "CPU: opcode JAM $%02X en $%04X; parada hasta el reset\n",
                   opcode, pc_);
      break;
  }

  const bool diferida = o.op == CLI || o.op == SEI || o.op == PLP;
  i_muestreado_ = (diferida ? i_previo : (uint8_t)(sr_ & FI)) != 0;
  ciclos_totales_ += (uint64_t)last_cycles_;
}
