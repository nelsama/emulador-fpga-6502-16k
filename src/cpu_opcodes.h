// Instrucción y modo de direccionamiento de los 256 opcodes, sacados de la tabla
// opcodeInfoTable de cpu65xx_fast.vhd (incluidos los ilegales). JAM = la CPU se para.
#pragma once

#include <cstdint>

enum Op : uint8_t {
  BRK,
  ORA, JAM, SLO, NOP, ASL, PHP, ANC, BPL, CLC, JSR, AND, RLA,
  BIT, ROL, PLP, BMI, SEC, RTI, EOR, SRE, LSR, PHA, ALR, JMP,
  BVC, CLI, RTS, ADC, RRA, ROR, PLA, ARR, BVS, SEI, STA, SAX,
  STY, STX, DEY, TXA, ANE, BCC, SHA, TYA, TXS, SHS, SHY, SHX,
  LDY, LDA, LDX, LAX, TAY, TAX, LXA, BCS, CLV, TSX, LAS, CPY,
  CMP, DCP, DEC, INY, DEX, SBX, BNE, CLD, CPX, SBC, ISC, INC, INX, BEQ, SED
};
enum Modo : uint8_t { IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IZX, IZY, REL, IND };

struct Opcode {
  Op op;
  Modo modo;
};

static constexpr Opcode kOpcodes[256] = {
    {BRK,IMP}, {ORA,IZX}, {JAM,IMP}, {SLO,IZX}, {NOP,ZP}, {ORA,ZP}, {ASL,ZP}, {SLO,ZP},  // 00
    {PHP,IMP}, {ORA,IMM}, {ASL,ACC}, {ANC,IMM}, {NOP,ABS}, {ORA,ABS}, {ASL,ABS}, {SLO,ABS},  // 08
    {BPL,REL}, {ORA,IZY}, {JAM,IMP}, {SLO,IZY}, {NOP,ZPX}, {ORA,ZPX}, {ASL,ZPX}, {SLO,ZPX},  // 10
    {CLC,IMP}, {ORA,ABY}, {NOP,IMP}, {SLO,ABY}, {NOP,ABX}, {ORA,ABX}, {ASL,ABX}, {SLO,ABX},  // 18
    {JSR,ABS}, {AND,IZX}, {JAM,IMP}, {RLA,IZX}, {BIT,ZP}, {AND,ZP}, {ROL,ZP}, {RLA,ZP},  // 20
    {PLP,IMP}, {AND,IMM}, {ROL,ACC}, {ANC,IMM}, {BIT,ABS}, {AND,ABS}, {ROL,ABS}, {RLA,ABS},  // 28
    {BMI,REL}, {AND,IZY}, {JAM,IMP}, {RLA,IZY}, {NOP,ZPX}, {AND,ZPX}, {ROL,ZPX}, {RLA,ZPX},  // 30
    {SEC,IMP}, {AND,ABY}, {NOP,IMP}, {RLA,ABY}, {NOP,ABX}, {AND,ABX}, {ROL,ABX}, {RLA,ABX},  // 38
    {RTI,IMP}, {EOR,IZX}, {JAM,IMP}, {SRE,IZX}, {NOP,ZP}, {EOR,ZP}, {LSR,ZP}, {SRE,ZP},  // 40
    {PHA,IMP}, {EOR,IMM}, {LSR,ACC}, {ALR,IMM}, {JMP,ABS}, {EOR,ABS}, {LSR,ABS}, {SRE,ABS},  // 48
    {BVC,REL}, {EOR,IZY}, {JAM,IMP}, {SRE,IZY}, {NOP,ZPX}, {EOR,ZPX}, {LSR,ZPX}, {SRE,ZPX},  // 50
    {CLI,IMP}, {EOR,ABY}, {NOP,IMP}, {SRE,ABY}, {NOP,ABX}, {EOR,ABX}, {LSR,ABX}, {SRE,ABX},  // 58
    {RTS,IMP}, {ADC,IZX}, {JAM,IMP}, {RRA,IZX}, {NOP,ZP}, {ADC,ZP}, {ROR,ZP}, {RRA,ZP},  // 60
    {PLA,IMP}, {ADC,IMM}, {ROR,ACC}, {ARR,IMM}, {JMP,IND}, {ADC,ABS}, {ROR,ABS}, {RRA,ABS},  // 68
    {BVS,REL}, {ADC,IZY}, {JAM,IMP}, {RRA,IZY}, {NOP,ZPX}, {ADC,ZPX}, {ROR,ZPX}, {RRA,ZPX},  // 70
    {SEI,IMP}, {ADC,ABY}, {NOP,IMP}, {RRA,ABY}, {NOP,ABX}, {ADC,ABX}, {ROR,ABX}, {RRA,ABX},  // 78
    {NOP,IMM}, {STA,IZX}, {NOP,IMM}, {SAX,IZX}, {STY,ZP}, {STA,ZP}, {STX,ZP}, {SAX,ZP},  // 80
    {DEY,IMP}, {NOP,IMM}, {TXA,IMP}, {ANE,IMM}, {STY,ABS}, {STA,ABS}, {STX,ABS}, {SAX,ABS},  // 88
    {BCC,REL}, {STA,IZY}, {JAM,IMP}, {SHA,IZY}, {STY,ZPX}, {STA,ZPX}, {STX,ZPY}, {SAX,ZPY},  // 90
    {TYA,IMP}, {STA,ABY}, {TXS,IMP}, {SHS,ABY}, {SHY,ABX}, {STA,ABX}, {SHX,ABY}, {SHA,ABY},  // 98
    {LDY,IMM}, {LDA,IZX}, {LDX,IMM}, {LAX,IZX}, {LDY,ZP}, {LDA,ZP}, {LDX,ZP}, {LAX,ZP},  // A0
    {TAY,IMP}, {LDA,IMM}, {TAX,IMP}, {LXA,IMM}, {LDY,ABS}, {LDA,ABS}, {LDX,ABS}, {LAX,ABS},  // A8
    {BCS,REL}, {LDA,IZY}, {JAM,IMP}, {LAX,IZY}, {LDY,ZPX}, {LDA,ZPX}, {LDX,ZPY}, {LAX,ZPY},  // B0
    {CLV,IMP}, {LDA,ABY}, {TSX,IMP}, {LAS,ABY}, {LDY,ABX}, {LDA,ABX}, {LDX,ABY}, {LAX,ABY},  // B8
    {CPY,IMM}, {CMP,IZX}, {NOP,IMM}, {DCP,IZX}, {CPY,ZP}, {CMP,ZP}, {DEC,ZP}, {DCP,ZP},  // C0
    {INY,IMP}, {CMP,IMM}, {DEX,IMP}, {SBX,IMM}, {CPY,ABS}, {CMP,ABS}, {DEC,ABS}, {DCP,ABS},  // C8
    {BNE,REL}, {CMP,IZY}, {JAM,IMP}, {DCP,IZY}, {NOP,ZPX}, {CMP,ZPX}, {DEC,ZPX}, {DCP,ZPX},  // D0
    {CLD,IMP}, {CMP,ABY}, {NOP,IMP}, {DCP,ABY}, {NOP,ABX}, {CMP,ABX}, {DEC,ABX}, {DCP,ABX},  // D8
    {CPX,IMM}, {SBC,IZX}, {NOP,IMM}, {ISC,IZX}, {CPX,ZP}, {SBC,ZP}, {INC,ZP}, {ISC,ZP},  // E0
    {INX,IMP}, {SBC,IMM}, {NOP,IMP}, {SBC,IMM}, {CPX,ABS}, {SBC,ABS}, {INC,ABS}, {ISC,ABS},  // E8
    {BEQ,REL}, {SBC,IZY}, {JAM,IMP}, {ISC,IZY}, {NOP,ZPX}, {SBC,ZPX}, {INC,ZPX}, {ISC,ZPX},  // F0
    {SED,IMP}, {SBC,ABY}, {NOP,IMP}, {ISC,ABY}, {NOP,ABX}, {SBC,ABX}, {INC,ABX}, {ISC,ABX},  // F8
};
