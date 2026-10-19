#include "cpu_6502.h"

#include "bus.h"
#include "cpu_cycles.h"

Cpu6502::Cpu6502(Bus& bus) : bus_(bus) {}

uint8_t Cpu6502::read(uint16_t addr) { return bus_.read(addr); }
void    Cpu6502::write(uint16_t addr, uint8_t v) { bus_.write(addr, v); }

uint16_t Cpu6502::read16(uint16_t addr) {
  // Little-endian (el 6502 es little-endian).
  uint8_t lo = read(addr);
  uint8_t hi = read(addr + 1);
  return (uint16_t)(lo | (hi << 8));
}

void Cpu6502::set_z_n(uint8_t v) {
  if (v == 0) sr_ |= 0x02;  else sr_ &= ~0x02; // Z
  if (v & 0x80) sr_ |= 0x80; else sr_ &= ~0x80; // N
}

void Cpu6502::set_carry(bool c) {
  if (c) sr_ |= 0x01; else sr_ &= ~0x01; // C
}

void Cpu6502::reset() {
  // El vector de RESET está en $FFFC-$FFFD (que mapea a ROM $BFFC-$BFFD).
  pc_ = read16(0xFFFC);
  sp_ = 0xFD;
  sr_ = 0x24; // bit5 = 1 (unused), bit2 = 1 (I)
  a_ = x_ = y_ = 0;
}

void Cpu6502::irq() {
  if (sr_ & 0x04) return; // I flag set -> masked
  // push PC and flags
  write(0x0100 | sp_, (uint8_t)(pc_ >> 8)); sp_--;
  write(0x0100 | sp_, (uint8_t)(pc_ & 0xFF)); sp_--;
  write(0x0100 | sp_, sr_); sp_--;
  sr_ |= 0x04; // set I
  pc_ = read16(0xFFFE);
}

void Cpu6502::nmi() {
  write(0x0100 | sp_, (uint8_t)(pc_ >> 8)); sp_--;
  write(0x0100 | sp_, (uint8_t)(pc_ & 0xFF)); sp_--;
  write(0x0100 | sp_, sr_); sp_--;
  sr_ |= 0x04;
  pc_ = read16(0xFFFA);
}

void Cpu6502::step() {
  // Comprobar IRQ si la línea está activa (activa en bajo) y las interrupciones
  // no están enmascaradas.
  if (bus_.irq_asserted() && !(sr_ & 0x04)) {
    irq();
    last_cycles_ = 7;
    return;
  }

  uint8_t op = read(pc_++);
  last_cycles_ = kOpcodeCycles[op];

  switch (op) {
    // ---- LDA ----
    case 0xA9: a_ = read(pc_++); set_z_n(a_); break;               // imm
    case 0xA5: { uint8_t z = read(pc_++); a_ = read(z); set_z_n(a_); break; } // zp
    case 0xB5: { uint8_t z = read(pc_++); a_ = read((uint8_t)(z + x_)); set_z_n(a_); break; } // zp,x
    case 0xAD: { uint16_t a = read16(pc_); pc_ += 2; a_ = read(a); set_z_n(a_); break; } // abs
    case 0xBD: { uint16_t a = read16(pc_); pc_ += 2; a_ = read(a + x_); set_z_n(a_); break; } // abs,x
    case 0xB9: { uint16_t a = read16(pc_); pc_ += 2; a_ = read(a + y_); set_z_n(a_); break; } // abs,y
    case 0xA1: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ = read(a + x_); set_z_n(a_); break; } // (ind,x)
    case 0xB1: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ = read(a + y_); set_z_n(a_); break; } // (ind),y

    // ---- LDX ----
    case 0xA2: x_ = read(pc_++); set_z_n(x_); break;               // imm
    case 0xA6: { uint8_t z = read(pc_++); x_ = read(z); set_z_n(x_); break; } // zp
    case 0xB6: { uint8_t z = read(pc_++); x_ = read((uint8_t)(z + y_)); set_z_n(x_); break; } // zp,y
    case 0xAE: { uint16_t a = read16(pc_); pc_ += 2; x_ = read(a); set_z_n(x_); break; } // abs
    case 0xBE: { uint16_t a = read16(pc_); pc_ += 2; x_ = read(a + y_); set_z_n(x_); break; } // abs,y

    // ---- LDY ----
    case 0xA0: y_ = read(pc_++); set_z_n(y_); break;               // imm
    case 0xA4: { uint8_t z = read(pc_++); y_ = read(z); set_z_n(y_); break; } // zp
    case 0xB4: { uint8_t z = read(pc_++); y_ = read((uint8_t)(z + x_)); set_z_n(y_); break; } // zp,x
    case 0xAC: { uint16_t a = read16(pc_); pc_ += 2; y_ = read(a); set_z_n(y_); break; } // abs
    case 0xBC: { uint16_t a = read16(pc_); pc_ += 2; y_ = read(a + x_); set_z_n(y_); break; } // abs,x

    // ---- STA ----
    case 0x85: { uint8_t z = read(pc_++); write(z, a_); break; }    // zp
    case 0x95: { uint8_t z = read(pc_++); write((uint8_t)(z + x_), a_); break; } // zp,x
    case 0x8D: { uint16_t a = read16(pc_); pc_ += 2; write(a, a_); break; } // abs
    case 0x9D: { uint16_t a = read16(pc_); pc_ += 2; write(a + x_, a_); break; } // abs,x
    case 0x99: { uint16_t a = read16(pc_); pc_ += 2; write(a + y_, a_); break; } // abs,y
    case 0x81: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); write(a + x_, a_); break; } // (ind,x)
    case 0x91: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); write(a + y_, a_); break; } // (ind),y

    // ---- STX ----
    case 0x86: { uint8_t z = read(pc_++); write(z, x_); break; }    // zp
    case 0x96: { uint8_t z = read(pc_++); write((uint8_t)(z + y_), x_); break; } // zp,y
    case 0x8E: { uint16_t a = read16(pc_); pc_ += 2; write(a, x_); break; } // abs

    // ---- STY ----
    case 0x84: { uint8_t z = read(pc_++); write(z, y_); break; }    // zp
    case 0x94: { uint8_t z = read(pc_++); write((uint8_t)(z + x_), y_); break; } // zp,x
    case 0x8C: { uint16_t a = read16(pc_); pc_ += 2; write(a, y_); break; } // abs

    // ---- Transferencias ----
    case 0xAA: x_ = a_; set_z_n(x_); break; // TAX
    case 0xA8: y_ = a_; set_z_n(y_); break; // TAY
    case 0x8A: a_ = x_; set_z_n(a_); break; // TXA
    case 0x98: a_ = y_; set_z_n(a_); break; // TYA
    case 0x9A: sp_ = x_; break;             // TXS
    case 0xBA: x_ = sp_; set_z_n(x_); break;// TSX
    case 0x48: write(0x0100 | sp_, a_); sp_--; break; // PHA
    case 0x68: sp_++; a_ = read(0x0100 | sp_); set_z_n(a_); break; // PLA
    case 0x08: write(0x0100 | sp_, sr_ | 0x30); sp_--; break; // PHP
    case 0x28: sp_++; sr_ = read(0x0100 | sp_); break;        // PLP

    // ---- ORA (faltantes) ----
    case 0x01: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ |= read(a + x_); set_z_n(a_); break; } // (ind,x)
    case 0x11: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ |= read(a + y_); set_z_n(a_); break; } // (ind),y
    case 0x15: { uint8_t z = read(pc_++); a_ |= read((uint8_t)(z + x_)); set_z_n(a_); break; }   // zp,x
    case 0x19: { uint16_t a = read16(pc_); pc_ += 2; a_ |= read(a + y_); set_z_n(a_); break; } // abs,y
    case 0x1D: { uint16_t a = read16(pc_); pc_ += 2; a_ |= read(a + x_); set_z_n(a_); break; } // abs,x

    // ---- AND (faltantes) ----
    case 0x21: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ &= read(a + x_); set_z_n(a_); break; } // (ind,x)
    case 0x31: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ &= read(a + y_); set_z_n(a_); break; } // (ind),y
    case 0x35: { uint8_t z = read(pc_++); a_ &= read((uint8_t)(z + x_)); set_z_n(a_); break; }   // zp,x
    case 0x39: { uint16_t a = read16(pc_); pc_ += 2; a_ &= read(a + y_); set_z_n(a_); break; } // abs,y
    case 0x3D: { uint16_t a = read16(pc_); pc_ += 2; a_ &= read(a + x_); set_z_n(a_); break; } // abs,x

    // ---- EOR (faltantes) ----
    case 0x41: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ ^= read(a + x_); set_z_n(a_); break; } // (ind,x)
    case 0x51: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ ^= read(a + y_); set_z_n(a_); break; } // (ind),y
    case 0x55: { uint8_t z = read(pc_++); a_ ^= read((uint8_t)(z + x_)); set_z_n(a_); break; }   // zp,x
    case 0x4D: { uint16_t a = read16(pc_); pc_ += 2; a_ ^= read(a); set_z_n(a_); break; }       // abs
    case 0x59: { uint16_t a = read16(pc_); pc_ += 2; a_ ^= read(a + y_); set_z_n(a_); break; } // abs,y
    case 0x5D: { uint16_t a = read16(pc_); pc_ += 2; a_ ^= read(a + x_); set_z_n(a_); break; } // abs,x

    // ---- CMP (faltantes) ----
    case 0xC1: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); uint8_t m = read(a + x_); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // (ind,x)
    case 0xD1: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); uint8_t m = read(a + y_); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // (ind),y
    case 0xD5: { uint8_t z = read(pc_++); uint8_t m = read((uint8_t)(z + x_)); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // zp,x
    case 0xD9: { uint16_t a = read16(pc_); pc_ += 2; uint8_t m = read(a + y_); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // abs,y
    case 0xDD: { uint16_t a = read16(pc_); pc_ += 2; uint8_t m = read(a + x_); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // abs,x

    // ---- CPX / CPY (faltantes) ----
    case 0xE4: { uint8_t z = read(pc_++); uint8_t m = read(z); uint8_t r = x_ - m; set_z_n(r); set_carry(x_ >= m); break; } // CPX zp
    case 0xEC: { uint16_t a = read16(pc_); pc_ += 2; uint8_t m = read(a); uint8_t r = x_ - m; set_z_n(r); set_carry(x_ >= m); break; } // CPX abs
    case 0xC4: { uint8_t z = read(pc_++); uint8_t m = read(z); uint8_t r = y_ - m; set_z_n(r); set_carry(y_ >= m); break; } // CPY zp
    case 0xCC: { uint16_t a = read16(pc_); pc_ += 2; uint8_t m = read(a); uint8_t r = y_ - m; set_z_n(r); set_carry(y_ >= m); break; } // CPY abs

    // ---- LDA (faltantes) ----
    case 0xB2: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); a_ = read(a); set_z_n(a_); break; } // (ind)

    // ---- INC / DEC (faltantes) ----
    case 0xFE: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a + x_) + 1; write(a + x_, v); set_z_n(v); break; } // INC abs,x
    case 0xD6: { uint8_t z = read(pc_++); uint8_t v = read((uint8_t)(z + x_)) - 1; write((uint8_t)(z + x_), v); set_z_n(v); break; } // DEC zp,x
    case 0xDE: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a + x_) - 1; write(a + x_, v); set_z_n(v); break; } // DEC abs,x

    // ---- Shifts / Rotates en memoria (faltantes) ----
    case 0x06: { uint8_t z = read(pc_++); uint8_t v = read(z); bool c = v & 0x80; v <<= 1; write(z, v); set_z_n(v); set_carry(c); break; } // ASL zp
    case 0x16: { uint8_t z = read(pc_++); uint8_t e = (uint8_t)(z + x_); uint8_t v = read(e); bool c = v & 0x80; v <<= 1; write(e, v); set_z_n(v); set_carry(c); break; } // ASL zp,x
    case 0x0E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a); bool c = v & 0x80; v <<= 1; write(a, v); set_z_n(v); set_carry(c); break; } // ASL abs
    case 0x1E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t e = a + x_; uint8_t v = read(e); bool c = v & 0x80; v <<= 1; write(e, v); set_z_n(v); set_carry(c); break; } // ASL abs,x
    case 0x46: { uint8_t z = read(pc_++); uint8_t v = read(z); bool c = v & 0x01; v >>= 1; write(z, v); set_z_n(v); set_carry(c); break; } // LSR zp
    case 0x56: { uint8_t z = read(pc_++); uint8_t e = (uint8_t)(z + x_); uint8_t v = read(e); bool c = v & 0x01; v >>= 1; write(e, v); set_z_n(v); set_carry(c); break; } // LSR zp,x
    case 0x4E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a); bool c = v & 0x01; v >>= 1; write(a, v); set_z_n(v); set_carry(c); break; } // LSR abs
    case 0x5E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t e = a + x_; uint8_t v = read(e); bool c = v & 0x01; v >>= 1; write(e, v); set_z_n(v); set_carry(c); break; } // LSR abs,x
    case 0x26: { uint8_t z = read(pc_++); uint8_t v = read(z); bool c = v & 0x80; v = (uint8_t)((v << 1) | (sr_ & 0x01)); write(z, v); set_z_n(v); set_carry(c); break; } // ROL zp
    case 0x36: { uint8_t z = read(pc_++); uint8_t e = (uint8_t)(z + x_); uint8_t v = read(e); bool c = v & 0x80; v = (uint8_t)((v << 1) | (sr_ & 0x01)); write(e, v); set_z_n(v); set_carry(c); break; } // ROL zp,x
    case 0x2E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a); bool c = v & 0x80; v = (uint8_t)((v << 1) | (sr_ & 0x01)); write(a, v); set_z_n(v); set_carry(c); break; } // ROL abs
    case 0x3E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t e = a + x_; uint8_t v = read(e); bool c = v & 0x80; v = (uint8_t)((v << 1) | (sr_ & 0x01)); write(e, v); set_z_n(v); set_carry(c); break; } // ROL abs,x
    case 0x66: { uint8_t z = read(pc_++); uint8_t v = read(z); bool c = v & 0x01; v = (uint8_t)((v >> 1) | ((sr_ & 0x01) << 7)); write(z, v); set_z_n(v); set_carry(c); break; } // ROR zp
    case 0x76: { uint8_t z = read(pc_++); uint8_t e = (uint8_t)(z + x_); uint8_t v = read(e); bool c = v & 0x01; v = (uint8_t)((v >> 1) | ((sr_ & 0x01) << 7)); write(e, v); set_z_n(v); set_carry(c); break; } // ROR zp,x
    case 0x6E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a); bool c = v & 0x01; v = (uint8_t)((v >> 1) | ((sr_ & 0x01) << 7)); write(a, v); set_z_n(v); set_carry(c); break; } // ROR abs
    case 0x7E: { uint16_t a = read16(pc_); pc_ += 2; uint8_t e = a + x_; uint8_t v = read(e); bool c = v & 0x01; v = (uint8_t)((v >> 1) | ((sr_ & 0x01) << 7)); write(e, v); set_z_n(v); set_carry(c); break; } // ROR abs,x

    // ---- BIT (faltantes) ----
    case 0x2C: { uint16_t a = read16(pc_); pc_ += 2; uint8_t m = read(a); if (!(a_ & m)) sr_ |= 0x02; else sr_ &= ~0x02; if (m & 0x80) sr_ |= 0x80; else sr_ &= ~0x80; if (m & 0x40) sr_ |= 0x40; else sr_ &= ~0x40; break; } // BIT abs

    // ---- Incrementos/decrementos ----
    case 0xE6: { uint8_t z = read(pc_++); uint8_t v = read(z) + 1; write(z, v); set_z_n(v); break; } // INC zp
    case 0xF6: { uint8_t z = read(pc_++); uint8_t v = read((uint8_t)(z + x_)) + 1; write((uint8_t)(z + x_), v); set_z_n(v); break; } // INC zp,x
    case 0xEE: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a) + 1; write(a, v); set_z_n(v); break; } // INC abs
    case 0xC6: { uint8_t z = read(pc_++); uint8_t v = read(z) - 1; write(z, v); set_z_n(v); break; } // DEC zp
    case 0xCE: { uint16_t a = read16(pc_); pc_ += 2; uint8_t v = read(a) - 1; write(a, v); set_z_n(v); break; } // DEC abs
    case 0xE8: x_++; set_z_n(x_); break; // INX
    case 0xC8: y_++; set_z_n(y_); break; // INY
    case 0xCA: x_--; set_z_n(x_); break; // DEX
    case 0x88: y_--; set_z_n(y_); break; // DEY

    // ---- Saltos ----
    case 0x4C: pc_ = read16(pc_); break; // JMP abs
    case 0x6C: { uint16_t a = read16(pc_); pc_ = read16(a); break; } // JMP (ind)
    case 0x20: { // JSR abs: empuja PC-1 (dirección del último byte de la
                 // instrucción) para que RTS haga +1 y vuelva a la siguiente.
      uint16_t tgt = read16(pc_);
      uint16_t ret = (uint16_t)(pc_ + 1); // PC apunta al opcode; +1 = último byte
      write(0x0100 | sp_, (uint8_t)(ret >> 8)); sp_--;
      write(0x0100 | sp_, (uint8_t)(ret & 0xFF)); sp_--;
      pc_ = tgt;
      break;
    }
    case 0x60: sp_++; { uint8_t lo = read(0x0100 | sp_); sp_++; uint8_t hi = read(0x0100 | sp_); pc_ = (hi << 8) | lo; pc_++; break; } // RTS
    case 0x40: sp_++; sr_ = read(0x0100 | sp_); sp_++; { uint8_t lo = read(0x0100 | sp_); sp_++; uint8_t hi = read(0x0100 | sp_); pc_ = (hi << 8) | lo; break; } // RTI

    // ---- Ramas ----
    case 0x10: { int8_t r = (int8_t)read(pc_++); if (!(sr_ & 0x80)) pc_ += r; break; } // BPL
    case 0x30: { int8_t r = (int8_t)read(pc_++); if (sr_ & 0x80) pc_ += r; break; }   // BMI
    case 0x50: { int8_t r = (int8_t)read(pc_++); if (!(sr_ & 0x40)) pc_ += r; break; } // BVC
    case 0x70: { int8_t r = (int8_t)read(pc_++); if (sr_ & 0x40) pc_ += r; break; }   // BVS
    case 0x90: { int8_t r = (int8_t)read(pc_++); if (!(sr_ & 0x01)) pc_ += r; break; } // BCC
    case 0xB0: { int8_t r = (int8_t)read(pc_++); if (sr_ & 0x01) pc_ += r; break; }   // BCS
    case 0xD0: { int8_t r = (int8_t)read(pc_++); if (!(sr_ & 0x02)) pc_ += r; break; } // BNE
    case 0xF0: { int8_t r = (int8_t)read(pc_++); if (sr_ & 0x02) pc_ += r; break; }   // BEQ

    // ---- Aritmética lógica ----
    case 0x0A: { bool c = a_ & 0x80; a_ <<= 1; set_z_n(a_); set_carry(c); break; } // ASL A
    case 0x4A: { bool c = a_ & 0x01; a_ >>= 1; set_z_n(a_); set_carry(c); break; } // LSR A
    case 0x2A: { bool c = a_ & 0x80; a_ = (a_ << 1) | (sr_ & 0x01); set_z_n(a_); set_carry(c); break; } // ROL A
    case 0x6A: { bool c = a_ & 0x01; a_ = (a_ >> 1) | ((sr_ & 0x01) << 7); set_z_n(a_); set_carry(c); break; } // ROR A

    case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1: { // SBC
      uint8_t m;
      switch (op) {
        case 0xE9: m = read(pc_++); break;
        case 0xE5: { uint8_t z = read(pc_++); m = read(z); break; }
        case 0xF5: { uint8_t z = read(pc_++); m = read((uint8_t)(z + x_)); break; }
        case 0xED: { uint16_t a = read16(pc_); pc_ += 2; m = read(a); break; }
        case 0xFD: { uint16_t a = read16(pc_); pc_ += 2; m = read(a + x_); break; }
        case 0xF9: { uint16_t a = read16(pc_); pc_ += 2; m = read(a + y_); break; }
        case 0xE1: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); m = read(a + x_); break; }
        case 0xF1: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); m = read(a + y_); break; }
        default: m = 0; break;
      }
      uint16_t r = a_ + (uint8_t)(~m) + (sr_ & 0x01);
      set_carry(r > 0xFF);
      // overflow: (a ^ r) & (m ^ r) & 0x80
      if ((a_ ^ (uint8_t)r) & ((uint8_t)(~m) ^ (uint8_t)r) & 0x80) sr_ |= 0x40; else sr_ &= ~0x40;
      a_ = (uint8_t)r;
      set_z_n(a_);
      break;
    }

    case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71: { // ADC
      uint8_t m;
      switch (op) {
        case 0x69: m = read(pc_++); break;
        case 0x65: { uint8_t z = read(pc_++); m = read(z); break; }
        case 0x75: { uint8_t z = read(pc_++); m = read((uint8_t)(z + x_)); break; }
        case 0x6D: { uint16_t a = read16(pc_); pc_ += 2; m = read(a); break; }
        case 0x7D: { uint16_t a = read16(pc_); pc_ += 2; m = read(a + x_); break; }
        case 0x79: { uint16_t a = read16(pc_); pc_ += 2; m = read(a + y_); break; }
        case 0x61: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); m = read(a + x_); break; }
        case 0x71: { uint8_t z = read(pc_++); uint16_t a = read(z) | (read((uint8_t)(z+1)) << 8); m = read(a + y_); break; }
        default: m = 0; break;
      }
      uint16_t r = a_ + m + (sr_ & 0x01);
      set_carry(r > 0xFF);
      if ((~(a_ ^ m) & (a_ ^ (uint8_t)r) & 0x80)) sr_ |= 0x40; else sr_ &= ~0x40;
      a_ = (uint8_t)r;
      set_z_n(a_);
      break;
    }

    case 0x29: a_ &= read(pc_++); set_z_n(a_); break; // AND imm
    case 0x25: { uint8_t z = read(pc_++); a_ &= read(z); set_z_n(a_); break; } // AND zp
    case 0x2D: { uint16_t a = read16(pc_); pc_ += 2; a_ &= read(a); set_z_n(a_); break; } // AND abs
    case 0x09: a_ |= read(pc_++); set_z_n(a_); break; // ORA imm
    case 0x05: { uint8_t z = read(pc_++); a_ |= read(z); set_z_n(a_); break; } // ORA zp
    case 0x0D: { uint16_t a = read16(pc_); pc_ += 2; a_ |= read(a); set_z_n(a_); break; } // ORA abs
    case 0x49: a_ ^= read(pc_++); set_z_n(a_); break; // EOR imm
    case 0x45: { uint8_t z = read(pc_++); a_ ^= read(z); set_z_n(a_); break; } // EOR zp

    case 0xC9: { uint8_t m = read(pc_++); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // CMP imm
    case 0xC5: { uint8_t z = read(pc_++); uint8_t m = read(z); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // CMP zp
    case 0xCD: { uint16_t a = read16(pc_); pc_ += 2; uint8_t m = read(a); uint8_t r = a_ - m; set_z_n(r); set_carry(a_ >= m); break; } // CMP abs
    case 0xE0: { uint8_t m = read(pc_++); uint8_t r = x_ - m; set_z_n(r); set_carry(x_ >= m); break; } // CPX imm
    case 0xC0: { uint8_t m = read(pc_++); uint8_t r = y_ - m; set_z_n(r); set_carry(y_ >= m); break; } // CPY imm

    case 0x24: { uint8_t z = read(pc_++); uint8_t m = read(z); if (!(a_ & m)) sr_ |= 0x02; else sr_ &= ~0x02; if (m & 0x80) sr_ |= 0x80; else sr_ &= ~0x80; if (m & 0x40) sr_ |= 0x40; else sr_ &= ~0x40; break; } // BIT zp

    // ---- Flags ----
    case 0x18: sr_ &= ~0x01; break; // CLC
    case 0x38: sr_ |= 0x01; break;  // SEC
    case 0x58: sr_ &= ~0x04; break; // CLI
    case 0x78: sr_ |= 0x04; break;  // SEI
    case 0xB8: sr_ &= ~0x40; break; // CLV
    case 0xD8: sr_ &= ~0x08; break; // CLD
    case 0xF8: sr_ |= 0x08; break;  // SED

    // ---- NOP / BRK ----
    case 0xEA: break; // NOP
    case 0x00: { // BRK
      pc_++; // skip padding byte
      write(0x0100 | sp_, (uint8_t)(pc_ >> 8)); sp_--;
      write(0x0100 | sp_, (uint8_t)(pc_ & 0xFF)); sp_--;
      write(0x0100 | sp_, sr_ | 0x30); sp_--;
      sr_ |= 0x04;
      pc_ = read16(0xFFFE);
      break;
    }

    default:
      // Instrucción no implementada. En una primera iteración la tratamos
      // como NOP para no romper el arranque; en producción hay que cubrirlas.
      break;
  }
}
