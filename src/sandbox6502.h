// sandbox6502.h — 64-byte MOS 6502 execution sandbox.
//
// The 6502 analog of sandbox.h, for the "different CPU" companion experiment to
// Cicala et al., "Co-evolution of self-replication and function in a digital
// primordial soup" (arXiv:2607.09211). Same protocol as the Z80 sandbox
// (Methods 4.1), transposed onto the accumulator-based 6502:
//   - two 32-byte programs concatenated -> 2*L = 64 byte memory
//   - address space limited to 2L bytes; EVERY bus access taken modulo 2L
//     (so zero-page 0..63 *is* the tape, and the stack folds onto it too)
//   - execute from PC=0 for at most B=512 instructions
//   - init: A=0xFF, S=0xFF, Y=0, P=0, PC=0; X = task input x (validation)
//           or 0 (interaction). Binary arithmetic (decimal mode disabled).
//   - output read from the accumulator A (8-bit -> result mod 2^8)
//
// Register mapping vs the Z80 sandbox:
//   Z80 D (input)  -> 6502 X   (index register; a solver reads it before its
//                               copy loop clobbers it)
//   Z80 E (output) -> 6502 A   (accumulator; the natural result register)
// Input and output are distinct registers, so a do-nothing program cannot win
// the identity task f(x)=x for free (exactly as E!=D on the Z80).
//
// "Halt": the 6502 has no HALT instruction. Its true analog is a JAM/KIL
// opcode (the 12 undocumented opcodes that freeze the CPU). We detect a JAM at
// opcode-fetch time and stop, which mirrors the Z80's HALT-stops-early rule and
// is what the hardware actually does. (chips decodes a JAM as an IR-- infinite
// stall; peeking the opcode lets us stop cleanly and deterministically.)
#pragma once
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>                  // the chips decoder calls bare assert() in
                                     // unreachable cycle slots; -DNDEBUG in the
                                     // soup build makes them no-ops for speed.
#ifndef CHIPS_ASSERT
#define CHIPS_ASSERT(c) ((void)0)   // never abort the soup on a decoder edge case
#endif
#include "m6502.h"

#define SB6_MEM    64      // 2 * L, L = 32
#define SB6_MASK   (SB6_MEM - 1)
#define SB6_BUDGET 512     // instruction budget B

typedef struct {
    uint8_t out_a;   // accumulator A at end (the program output)
    int     steps;   // number of instructions executed (<= budget)
    int     halted;  // 1 if the program hit a JAM before exhausting the budget
} sb6_result;

// The 12 KIL/JAM/HLT undocumented opcodes that freeze a real 6502.
static inline int sb6_is_jam(uint8_t op){
    switch(op){
        case 0x02: case 0x12: case 0x22: case 0x32:
        case 0x42: case 0x52: case 0x62: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2: return 1;
        default: return 0;
    }
}

// Run `mem` (64 bytes, program(s) already laid out) for at most `budget`
// instructions, with X initialized to `x_init`. Memory is modified in place
// (self-modification / copying persists). Returns output/steps/halted.
static inline sb6_result sb6_run(m6502_t* cpu, uint8_t* mem, uint8_t x_init, int budget){
    m6502_desc_t desc; memset(&desc, 0, sizeof desc);
    desc.bcd_disabled = true;          // binary ADC/SBC (matches the Z80's binary ops)
    m6502_init(cpu, &desc);
    cpu->A = 0xFF; cpu->X = x_init; cpu->Y = 0x00;
    cpu->S = 0xFF; cpu->P = 0x00; cpu->PC = 0x0000;

    // Present the first opcode fetch at PC=0, bypassing the 7-cycle reset
    // sequence (which would otherwise read a reset vector). This is the
    // documented "prefetch"/goto trick: assert SYNC with the bus at PC.
    uint64_t pins = M6502_SYNC | M6502_RW;
    M6502_SET_ADDR(pins, 0x0000);

    int steps = 0, halted = 0;
    // Cycle cap: worst-case documented instructions are ~7 cycles; 8*budget+16
    // is ample slack and also breaks any pathological stall.
    long tick_cap = (long)budget * 8 + 16;
    while (tick_cap-- > 0){
        const uint16_t addr = M6502_GET_ADDR(pins) & SB6_MASK;
        if (pins & M6502_SYNC){                 // opcode fetch = start of instruction
            if (steps >= budget) break;         // executed `budget` full instructions
            const uint8_t op = mem[addr];
            if (sb6_is_jam(op)){ halted = 1; break; }
            steps++;
            M6502_SET_DATA(pins, op);
        } else if (pins & M6502_RW){            // memory read
            M6502_SET_DATA(pins, mem[addr]);
        } else {                                // memory write
            mem[addr] = M6502_GET_DATA(pins);
        }
        pins = m6502_tick(cpu, pins);
    }
    sb6_result r;
    r.out_a  = cpu->A;
    r.steps  = steps;
    r.halted = halted;
    return r;
}
