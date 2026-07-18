// test_emu.c — correctness + benchmark for the 64-byte Z80 sandbox.
#define CHIPS_IMPL
#include "z80.h"
#include "sandbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t rng_state = 0x123456789abcdefULL;
static inline uint64_t xr() { // xorshift64
    uint64_t x = rng_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return rng_state = x;
}
static inline uint8_t rbyte() { return (uint8_t)(xr() >> 24); }

static void dump(const char* tag, uint8_t* m) {
    printf("%s: ", tag);
    for (int i = 0; i < SB_MEM; i++) { printf("%02X", m[i]); if (i==31) printf(" | "); }
    printf("\n");
}

int main(void) {
    z80_t cpu;

    // ---- Test 1: LDIR replicator seed [1E 20 ED B0] copies P1 -> P2 exactly ----
    {
        uint8_t m[SB_MEM];
        for (int i = 0; i < SB_MEM; i++) m[i] = 0;
        // P1 (bytes 0..31): LD E,0x20 ; LDIR ; then filler bytes
        uint8_t prog[4] = {0x1E, 0x20, 0xED, 0xB0};
        memcpy(m, prog, 4);
        for (int i = 4; i < 32; i++) m[i] = (uint8_t)(0x40 + i); // arbitrary distinctive filler
        for (int i = 32; i < 64; i++) m[i] = 0xAA;               // P2 region: to be overwritten
        sb_result r = sb_run(&cpu, m, 0 /*interaction D=0*/, SB_BUDGET);
        int ok = 1;
        for (int i = 0; i < 32; i++) if (m[i] != m[32+i]) { ok = 0; break; }
        printf("[Test1 LDIR replicator] steps=%d halted=%d  P2==P1: %s\n",
               r.steps, r.halted, ok ? "YES (exact self-copy)" : "NO");
        dump("  mem", m);
    }

    // ---- Test 2: Load-Push seed [(01 C5)*16] writes program into upper half ----
    {
        uint8_t m[SB_MEM];
        for (int i = 0; i < 32; i++)  m[i] = (i & 1) ? 0xC5 : 0x01;
        for (int i = 32; i < 64; i++) m[i] = 0x00;
        sb_result r = sb_run(&cpu, m, 0, SB_BUDGET);
        int nonzero = 0;
        for (int i = 32; i < 64; i++) if (m[i] != 0) nonzero++;
        printf("[Test2 Load-Push]  steps=%d halted=%d  upper-half bytes written=%d/32\n",
               r.steps, r.halted, nonzero);
        dump("  mem", m);
    }

    // ---- Test 3: hand-written polynomial 2n+1 -> register E ----
    // LD A,D (7A); ADD A,A (87); INC A (3C); LD E,A (5F); HALT (76)
    {
        uint8_t prog[5] = {0x7A, 0x87, 0x3C, 0x5F, 0x76};
        int all_ok = 1, max_steps = 0;
        for (int n = 0; n <= 15; n++) {
            uint8_t m[SB_MEM];
            memset(m, 0, SB_MEM);
            memcpy(m, prog, 5);
            sb_result r = sb_run(&cpu, m, (uint8_t)n, SB_BUDGET);
            uint8_t want = (uint8_t)((2*n + 1) & 0xFF);
            if (r.out_e != want || !r.halted) all_ok = 0;
            if (r.steps > max_steps) max_steps = r.steps;
        }
        printf("[Test3 2n+1 program]  all inputs correct & halted: %s  (max steps=%d)\n",
               all_ok ? "YES" : "NO", max_steps);
    }

    // ---- Test 4: hand-written n^2 (needs a loop; no MUL on Z80) ----
    // Compute A = n*n by adding n, n times. Uses B as counter.
    //   LD A,0     3E 00
    //   LD B,D     42        (B = n = loop count)
    //   loop: (if B==0 skip) ...
    // Simpler: A=0; C=D; B=D; loop: ADD A,C ; DJNZ loop ; LD E,A ; HALT
    //   3E 00     LD A,0
    //   4A        LD C,D    ; C = n
    //   42        LD B,D    ; B = n
    //   81        ADD A,C   ; A += n         <- loop start at addr 4
    //   10 FD     DJNZ -3   ; B--, jump to ADD if B!=0
    //   5F        LD E,A
    //   76        HALT
    {
        uint8_t prog[] = {0x3E,0x00, 0x4A, 0x42, 0x81, 0x10,0xFD, 0x5F, 0x76};
        int all_ok = 1, max_steps = 0;
        for (int n = 0; n <= 15; n++) {
            uint8_t m[SB_MEM];
            memset(m, 0, SB_MEM);
            memcpy(m, prog, sizeof prog);
            sb_result r = sb_run(&cpu, m, (uint8_t)n, SB_BUDGET);
            uint8_t want = (uint8_t)((n*n) & 0xFF);
            if (r.out_e != want) { all_ok = 0; printf("   n=%d got %d want %d\n", n, r.out_e, want); }
            if (r.steps > max_steps) max_steps = r.steps;
        }
        printf("[Test4 n^2 loop program]  all inputs correct: %s  (max steps=%d)\n",
               all_ok ? "YES" : "NO", max_steps);
    }

    // ---- Benchmark: throughput on random 64-byte tapes ----
    {
        const int N = 2000000;
        long total_steps = 0, halted = 0;
        // pre-generate is memory-heavy; just time the run loop with fresh random tapes
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int k = 0; k < N; k++) {
            uint8_t m[SB_MEM];
            for (int i = 0; i < SB_MEM; i++) m[i] = rbyte();
            uint8_t d = rbyte() & 0x0F;
            sb_result r = sb_run(&cpu, m, d, SB_BUDGET);
            total_steps += r.steps;
            halted += r.halted;
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        double avg_steps = (double)total_steps / N;
        printf("\n[Benchmark] %d random executions in %.3f s\n", N, secs);
        printf("  avg steps/exec = %.1f   halted early = %.1f%%\n",
               avg_steps, 100.0 * halted / N);
        printf("  executions/sec = %.2f M\n", N / secs / 1e6);
        printf("  instructions/sec = %.1f M\n", total_steps / secs / 1e6);
    }
    return 0;
}
