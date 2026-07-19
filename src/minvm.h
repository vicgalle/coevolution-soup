// minvm.h — a minimal, tunable-substrate VM for the "phase diagram of the origin
// of self-replication" experiment (research direction 1b, companion to the Z80
// paper arXiv:2607.09211 and the 6502 experiment in README_6502.md).
//
// WHY. The 6502 companion gave two data points — Z80 (has a 1-instruction block
// move LDIR, minimal copier ~2-3 instr) and 6502 (no block move, needs a copy
// LOOP, minimal copier ~6 instr) — and a mechanism: the *origin* difficulty of
// replication is governed by how many instructions must line up by chance to
// form a self-copier (call it L*), which fixes the spontaneous-replicator
// density, which fixes how long de-novo origin takes. Two points make a line
// only by assertion. This VM turns L* into a DIAL so we can draw the curve and
// test the predicted law:
//
//        density  rho(L*)  ~  base^(-L*)          (exponential in copier length)
//        T_origin          ~  1 / rho             (rarer copier -> later origin)
//
// and check that the real Z80/6502 fall on the same T_origin-vs-rho line.
//
// THE DIAL. A single copy primitive COPY moves g bytes per instruction. To copy
// the whole L=32-byte genome you need L/g of them, so the minimal self-copier is
//        SETD ; COPY x (L/g)          ->   L* = 1 + L/g  instructions.
// Sweeping g in {32,16,8,4,2} sweeps L* in {2,3,5,9,17}. There are NO jumps, so
// copying must be UNROLLED and L* is set *solely* by g. (A looped copier — the
// 6502's route — keeps a fixed L* regardless of g; that is a separate reference
// point, not part of this unrolled family.) g=1 needs L*=33 > 32 bytes: it does
// not fit in a genome at all, i.e. WITHOUT either a block move or a loop,
// replication cannot originate — exactly why the 6502 must loop.
//
// SECOND DIAL. Opcode class = byte % NB, with class 1 -> COPY and class 2 ->
// SETD, everything else NOP. So p(COPY) = p(SETD) = 1/NB. Raising NB dilutes the
// opcode space and lowers rho at FIXED L*, letting us move rho two independent
// ways and confirm T_origin tracks rho, not L* per se.
//
// EXECUTION MODEL (deliberately minimal, to isolate the L* variable):
//   - genome = L=32 bytes; two concatenated -> 2L=64-byte memory; addr mod 2L
//   - 1 byte per instruction; PC starts 0, +1 per instr
//   - COPY : for k in [0,g): mem[D+k] = mem[S+k];  then S+=g, D+=g   (mod 2L)
//   - SETD : S := 0, D := L      (aim read head at own start, write head at partner)
//   - NOP  : nothing (byte 0 -> class 0 -> NOP, an inert zero byte, Z80-like)
//   - STOP when PC >= L (a program runs only its own genome; this prevents the
//     freshly-written partner copy from being re-executed and corrupted) or
//     after `budget` instructions.
//   - registers S, D initialise to 0, so SETD is REQUIRED to point at the
//     partner: a do-nothing / all-zero genome copies nothing and is not counted.
#pragma once
#include <string.h>
#include <stdint.h>

#define MV_L      32          // genome length (matches the paper's 32-byte tape)
#define MV_MEM    (2*MV_L)    // concatenated tape
#define MV_MASK   (MV_MEM-1)  // 2L = 64 is a power of two

// Run the L-byte program that sits at mem[0..L) over the 2L-byte `mem` in place.
// g = copy granularity, nb = number of opcode buckets, budget = max instructions.
static inline void mv_run(uint8_t* mem, int g, int nb, int budget){
    int S = 0, D = 0, PC = 0, steps = 0;
    while (PC < MV_L && steps < budget){
        int op = mem[PC] % nb;
        PC++; steps++;
        if (op == 1){                       // COPY g bytes, advance both heads
            for (int k = 0; k < g; k++)
                mem[(D + k) & MV_MASK] = mem[(S + k) & MV_MASK];
            S = (S + g) & MV_MASK;
            D = (D + g) & MV_MASK;
        } else if (op == 2){                // SETD: aim source at start, dest at partner
            S = 0; D = MV_L;
        }                                   // else NOP
    }
}

// Functional self-copy test, mirroring density.c: lay the program against a
// fixed NON-ZERO partner and count how many of the L partner bytes end up equal
// to the original program (forward alignment). >= 30/32 counts as a self-copier.
static inline int mv_self_copy(const uint8_t* prog, int g, int nb, int budget){
    uint8_t m[MV_MEM];
    memcpy(m, prog, MV_L);
    for (int i = 0; i < MV_L; i++) m[MV_L + i] = (uint8_t)(0x5A ^ (i * 13));
    mv_run(m, g, nb, budget);
    int fwd = 0;
    for (int i = 0; i < MV_L; i++) if (m[MV_L + i] == prog[i]) fwd++;
    return fwd;
}

// Minimal-copier instruction count L* = 1 (SETD) + L/g (COPYs) for a divisor g.
static inline int mv_lstar(int g){ return 1 + (MV_L + g - 1) / g; }
