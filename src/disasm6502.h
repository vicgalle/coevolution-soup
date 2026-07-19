// disasm6502.h — minimal MOS 6502 disassembler for reading evolved genotypes.
// Full documented instruction set + the common undocumented opcodes; formats
// the first N instructions of a 32-byte tape. Addresses are shown as-is (the
// sandbox takes them modulo 64 at run time).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { _IMP,_ACC,_IMM,_ZP,_ZPX,_ZPY,_IZX,_IZY,_REL,_ABS,_ABX,_ABY,_IND };
static const uint8_t _d6_len[13] = {1,1,2,2,2,2,2,2,2,3,3,3,3};

typedef struct { const char* mn; uint8_t mode; } _d6_op;
static const _d6_op _D6[256] = {
/*00*/{"BRK",_IMP},{"ORA",_IZX},{"JAM",_IMP},{"SLO",_IZX},{"NOP",_ZP },{"ORA",_ZP },{"ASL",_ZP },{"SLO",_ZP },
/*08*/{"PHP",_IMP},{"ORA",_IMM},{"ASL",_ACC},{"ANC",_IMM},{"NOP",_ABS},{"ORA",_ABS},{"ASL",_ABS},{"SLO",_ABS},
/*10*/{"BPL",_REL},{"ORA",_IZY},{"JAM",_IMP},{"SLO",_IZY},{"NOP",_ZPX},{"ORA",_ZPX},{"ASL",_ZPX},{"SLO",_ZPX},
/*18*/{"CLC",_IMP},{"ORA",_ABY},{"NOP",_IMP},{"SLO",_ABY},{"NOP",_ABX},{"ORA",_ABX},{"ASL",_ABX},{"SLO",_ABX},
/*20*/{"JSR",_ABS},{"AND",_IZX},{"JAM",_IMP},{"RLA",_IZX},{"BIT",_ZP },{"AND",_ZP },{"ROL",_ZP },{"RLA",_ZP },
/*28*/{"PLP",_IMP},{"AND",_IMM},{"ROL",_ACC},{"ANC",_IMM},{"BIT",_ABS},{"AND",_ABS},{"ROL",_ABS},{"RLA",_ABS},
/*30*/{"BMI",_REL},{"AND",_IZY},{"JAM",_IMP},{"RLA",_IZY},{"NOP",_ZPX},{"AND",_ZPX},{"ROL",_ZPX},{"RLA",_ZPX},
/*38*/{"SEC",_IMP},{"AND",_ABY},{"NOP",_IMP},{"RLA",_ABY},{"NOP",_ABX},{"AND",_ABX},{"ROL",_ABX},{"RLA",_ABX},
/*40*/{"RTI",_IMP},{"EOR",_IZX},{"JAM",_IMP},{"SRE",_IZX},{"NOP",_ZP },{"EOR",_ZP },{"LSR",_ZP },{"SRE",_ZP },
/*48*/{"PHA",_IMP},{"EOR",_IMM},{"LSR",_ACC},{"ALR",_IMM},{"JMP",_ABS},{"EOR",_ABS},{"LSR",_ABS},{"SRE",_ABS},
/*50*/{"BVC",_REL},{"EOR",_IZY},{"JAM",_IMP},{"SRE",_IZY},{"NOP",_ZPX},{"EOR",_ZPX},{"LSR",_ZPX},{"SRE",_ZPX},
/*58*/{"CLI",_IMP},{"EOR",_ABY},{"NOP",_IMP},{"SRE",_ABY},{"NOP",_ABX},{"EOR",_ABX},{"LSR",_ABX},{"SRE",_ABX},
/*60*/{"RTS",_IMP},{"ADC",_IZX},{"JAM",_IMP},{"RRA",_IZX},{"NOP",_ZP },{"ADC",_ZP },{"ROR",_ZP },{"RRA",_ZP },
/*68*/{"PLA",_IMP},{"ADC",_IMM},{"ROR",_ACC},{"ARR",_IMM},{"JMP",_IND},{"ADC",_ABS},{"ROR",_ABS},{"RRA",_ABS},
/*70*/{"BVS",_REL},{"ADC",_IZY},{"JAM",_IMP},{"RRA",_IZY},{"NOP",_ZPX},{"ADC",_ZPX},{"ROR",_ZPX},{"RRA",_ZPX},
/*78*/{"SEI",_IMP},{"ADC",_ABY},{"NOP",_IMP},{"RRA",_ABY},{"NOP",_ABX},{"ADC",_ABX},{"ROR",_ABX},{"RRA",_ABX},
/*80*/{"NOP",_IMM},{"STA",_IZX},{"NOP",_IMM},{"SAX",_IZX},{"STY",_ZP },{"STA",_ZP },{"STX",_ZP },{"SAX",_ZP },
/*88*/{"DEY",_IMP},{"NOP",_IMM},{"TXA",_IMP},{"XAA",_IMM},{"STY",_ABS},{"STA",_ABS},{"STX",_ABS},{"SAX",_ABS},
/*90*/{"BCC",_REL},{"STA",_IZY},{"JAM",_IMP},{"AHX",_IZY},{"STY",_ZPX},{"STA",_ZPX},{"STX",_ZPY},{"SAX",_ZPY},
/*98*/{"TYA",_IMP},{"STA",_ABY},{"TXS",_IMP},{"TAS",_ABY},{"SHY",_ABX},{"STA",_ABX},{"SHX",_ABY},{"AHX",_ABY},
/*A0*/{"LDY",_IMM},{"LDA",_IZX},{"LDX",_IMM},{"LAX",_IZX},{"LDY",_ZP },{"LDA",_ZP },{"LDX",_ZP },{"LAX",_ZP },
/*A8*/{"TAY",_IMP},{"LDA",_IMM},{"TAX",_IMP},{"LAX",_IMM},{"LDY",_ABS},{"LDA",_ABS},{"LDX",_ABS},{"LAX",_ABS},
/*B0*/{"BCS",_REL},{"LDA",_IZY},{"JAM",_IMP},{"LAX",_IZY},{"LDY",_ZPX},{"LDA",_ZPX},{"LDX",_ZPY},{"LAX",_ZPY},
/*B8*/{"CLV",_IMP},{"LDA",_ABY},{"TSX",_IMP},{"LAS",_ABY},{"LDY",_ABX},{"LDA",_ABX},{"LDX",_ABY},{"LAX",_ABY},
/*C0*/{"CPY",_IMM},{"CMP",_IZX},{"NOP",_IMM},{"DCP",_IZX},{"CPY",_ZP },{"CMP",_ZP },{"DEC",_ZP },{"DCP",_ZP },
/*C8*/{"INY",_IMP},{"CMP",_IMM},{"DEX",_IMP},{"AXS",_IMM},{"CPY",_ABS},{"CMP",_ABS},{"DEC",_ABS},{"DCP",_ABS},
/*D0*/{"BNE",_REL},{"CMP",_IZY},{"JAM",_IMP},{"DCP",_IZY},{"NOP",_ZPX},{"CMP",_ZPX},{"DEC",_ZPX},{"DCP",_ZPX},
/*D8*/{"CLD",_IMP},{"CMP",_ABY},{"NOP",_IMP},{"DCP",_ABY},{"NOP",_ABX},{"CMP",_ABX},{"DEC",_ABX},{"DCP",_ABX},
/*E0*/{"CPX",_IMM},{"SBC",_IZX},{"NOP",_IMM},{"ISC",_IZX},{"CPX",_ZP },{"SBC",_ZP },{"INC",_ZP },{"ISC",_ZP },
/*E8*/{"INX",_IMP},{"SBC",_IMM},{"NOP",_IMP},{"SBC",_IMM},{"CPX",_ABS},{"SBC",_ABS},{"INC",_ABS},{"ISC",_ABS},
/*F0*/{"BEQ",_REL},{"SBC",_IZY},{"JAM",_IMP},{"ISC",_IZY},{"NOP",_ZPX},{"SBC",_ZPX},{"INC",_ZPX},{"ISC",_ZPX},
/*F8*/{"SED",_IMP},{"SBC",_ABY},{"NOP",_IMP},{"ISC",_ABY},{"NOP",_ABX},{"SBC",_ABX},{"INC",_ABX},{"ISC",_ABX},
};

// Disassemble up to `ninsn` instructions starting at tape offset 0 into `out`.
static void disasm6502_tape(const uint8_t* t, int ninsn, char* out, int outsz){
    int pc = 0, n = 0, w = 0;
    out[0] = 0;
    while (n < ninsn && pc < 32){
        uint8_t op = t[pc];
        const _d6_op* d = &_D6[op];
        int len = _d6_len[d->mode];
        char frag[48];
        uint8_t o1 = t[(pc+1)&31], o2 = t[(pc+2)&31];
        switch(d->mode){
            case _IMP: snprintf(frag,sizeof frag,"%s",d->mn); break;
            case _ACC: snprintf(frag,sizeof frag,"%s A",d->mn); break;
            case _IMM: snprintf(frag,sizeof frag,"%s #$%02X",d->mn,o1); break;
            case _ZP : snprintf(frag,sizeof frag,"%s $%02X",d->mn,o1); break;
            case _ZPX: snprintf(frag,sizeof frag,"%s $%02X,X",d->mn,o1); break;
            case _ZPY: snprintf(frag,sizeof frag,"%s $%02X,Y",d->mn,o1); break;
            case _IZX: snprintf(frag,sizeof frag,"%s ($%02X,X)",d->mn,o1); break;
            case _IZY: snprintf(frag,sizeof frag,"%s ($%02X),Y",d->mn,o1); break;
            case _REL:{ int8_t r=(int8_t)o1; snprintf(frag,sizeof frag,"%s $%02X",d->mn,(uint8_t)((pc+2+r)&0xFF)); } break;
            case _ABS: snprintf(frag,sizeof frag,"%s $%02X%02X",d->mn,o2,o1); break;
            case _ABX: snprintf(frag,sizeof frag,"%s $%02X%02X,X",d->mn,o2,o1); break;
            case _ABY: snprintf(frag,sizeof frag,"%s $%02X%02X,Y",d->mn,o2,o1); break;
            case _IND: snprintf(frag,sizeof frag,"%s ($%02X%02X)",d->mn,o2,o1); break;
            default:   snprintf(frag,sizeof frag,".byte $%02X",op); break;
        }
        int need = (int)strlen(frag) + 3;
        if (w + need >= outsz) break;
        w += snprintf(out+w, outsz-w, "%s%s", n?" ; ":"", frag);
        pc += len; n++;
        if (d->mode==_IMP && d->mn[0]=='J' && d->mn[1]=='A') break; // stop at JAM
    }
}
