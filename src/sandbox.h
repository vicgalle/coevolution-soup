// sandbox.h — 64-byte Z80 execution sandbox matching the paper's protocol.
//
// Cicala et al., "Co-evolution of self-replication and function in a digital
// primordial soup" (arXiv:2607.09211). Methods 4.1:
//   - two 32-byte programs concatenated -> 2*L = 64 byte memory
//   - address space limited to 2L bytes; every access taken modulo 2L
//   - execute from PC=0 for at most B=512 instructions
//   - init: A=SP=0xFF, HL=BC=E=0, D=input (validation) or 0 (interaction),
//           PC=0, everything else 0
//   - output read from register E (8-bit -> result mod 2^8)
#pragma once
#include <string.h>
#include <stdint.h>
#include "z80.h"

#define SB_MEM   64      // 2 * L, L = 32
#define SB_MASK  (SB_MEM - 1)
#define SB_BUDGET 512    // instruction budget B

// Result of one sandbox execution.
typedef struct {
    uint8_t  out_e;   // register E at end (the program output)
    int      steps;   // number of instructions executed (<= budget)
    int      halted;  // 1 if program hit HALT before exhausting the budget
} sb_result;

// Run `mem` (64 bytes, program(s) already laid out) for at most `budget`
// instructions, with register D initialized to `d_init`. Memory is modified in
// place (self-modification / copying persists). Returns output/steps/halted.
static inline sb_result sb_run(z80_t* cpu, uint8_t* mem, uint8_t d_init, int budget) {
    uint64_t pins = z80_init(cpu);
    // Impose the paper's register initialization, then re-prime the M1 fetch.
    cpu->a = 0xFF; cpu->f = 0x00;
    cpu->bc = 0x0000;
    cpu->de = (uint16_t)d_init << 8;   // D = d_init, E = 0
    cpu->hl = 0x0000;
    cpu->sp = 0x00FF;
    cpu->ix = cpu->iy = 0x0000;
    cpu->wz = 0x0000;
    cpu->ir = 0x0000;
    cpu->af2 = cpu->bc2 = cpu->de2 = cpu->hl2 = 0x0000;
    cpu->im = 0; cpu->iff1 = cpu->iff2 = false;
    pins = z80_prefetch(cpu, 0x0000);

    int steps = 0;
    int halted = 0;
    // Safety cap on ticks: the slowest Z80 instructions are ~23 T-states; the
    // block ops re-fetch per byte (still one opdone each), so 64*budget is ample.
    long tick_cap = (long)budget * 64 + 64;
    while (steps < budget && tick_cap-- > 0) {
        pins = z80_tick(cpu, pins);
        if (pins & Z80_MREQ) {
            const uint16_t addr = Z80_GET_ADDR(pins) & SB_MASK;
            if (pins & Z80_RD) {
                Z80_SET_DATA(pins, mem[addr]);
            } else if (pins & Z80_WR) {
                mem[addr] = Z80_GET_DATA(pins);
            }
        } else if (pins & Z80_IORQ) {
            // No I/O devices in the soup; reads return open-bus 0xFF, writes sink.
            if (pins & Z80_RD) { Z80_SET_DATA(pins, 0xFF); }
        }
        if (z80_opdone(cpu)) {
            steps++;
            if (pins & Z80_HALT) { halted = 1; break; }
        }
    }
    sb_result r;
    r.out_e  = cpu->e;
    r.steps  = steps;
    r.halted = halted;
    return r;
}
