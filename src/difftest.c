// difftest.c — differential test of z80fast.h against the chips z80.h
// reference (via sandbox.h's sb_run) over millions of random 64-byte tapes.
//
// Build: clang -O3 -march=native -o build/difftest src/difftest.c
//
// For each random program: run both emulators on copies of the same tape and
// compare final 64-byte memory, out_e, steps and halted. Also checks the LDIR
// replicator seed and benchmarks single-thread throughput of both emulators.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifndef CHIPS_IMPL
#define CHIPS_IMPL
#endif
#include "z80.h"     // reference emulator (implementation compiled here)
#include "sandbox.h" // sb_run protocol on top of z80.h
#include "z80fast.h" // fast emulator under test

#define N_DEFAULT 3000000
#define TAPE 64

// --- deterministic RNG (splitmix64)
static inline uint64_t sm64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void gen_tape(uint64_t* s, uint8_t* tape) {
    for (int i = 0; i < TAPE; i += 8) {
        uint64_t v = sm64(s);
        memcpy(tape + i, &v, 8);
    }
}

static void print_tape(const uint8_t* tape) {
    for (int i = 0; i < TAPE; i++) {
        printf("%02X%s", tape[i], (i % 16 == 15) ? "\n" : " ");
    }
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

// run reference emulator on a copy of the tape
static sb_result run_ref(const uint8_t* tape, uint8_t d_init, uint8_t* mem_out) {
    z80_t cpu;
    memcpy(mem_out, tape, TAPE);
    return sb_run(&cpu, mem_out, d_init, SB_BUDGET);
}

// run fast emulator on a copy of the tape
static sb_result run_fast(const uint8_t* tape, uint8_t d_init, uint8_t* mem_out) {
    sb_result r;
    memcpy(mem_out, tape, TAPE);
    r.steps = zf_run(mem_out, d_init, SB_BUDGET, &r.out_e, &r.halted);
    return r;
}

static int test_ldir_replicator(void) {
    // LDIR seed: 1E 20 ED B0 (LD E,0x20; LDIR), rest zero.
    uint8_t seed[TAPE];
    memset(seed, 0, TAPE);
    seed[0] = 0x1E; seed[1] = 0x20; seed[2] = 0xED; seed[3] = 0xB0;

    int ok = 1;
    for (uint8_t d = 0; d < 16; d++) {
        uint8_t mref[TAPE], mfast[TAPE];
        sb_result rr = run_ref(seed, d, mref);
        sb_result rf = run_fast(seed, d, mfast);
        if (memcmp(mref, mfast, TAPE) != 0 || rr.out_e != rf.out_e ||
            rr.steps != rf.steps || rr.halted != rf.halted) {
            printf("LDIR replicator MISMATCH at d_init=%u\n", d);
            printf("  ref : e=%02X steps=%d halted=%d\n", rr.out_e, rr.steps, rr.halted);
            printf("  fast: e=%02X steps=%d halted=%d\n", rf.out_e, rf.steps, rf.halted);
            printf("  ref mem:\n");  print_tape(mref);
            printf("  fast mem:\n"); print_tape(mfast);
            ok = 0;
        }
    }
    // partner-half check for d_init=0: after the run, the upper 32 bytes must
    // equal the lower 32 bytes (program copied itself into the partner half)
    {
        uint8_t mref[TAPE], mfast[TAPE];
        run_ref(seed, 0, mref);
        run_fast(seed, 0, mfast);
        if (memcmp(mref, mref + 32, 32) != 0) {
            printf("LDIR replicator: reference partner half does NOT match its lower half\n");
            print_tape(mref);
            ok = 0;
        }
        if (memcmp(mfast, mfast + 32, 32) != 0) {
            printf("LDIR replicator: fast partner half does NOT match its lower half\n");
            print_tape(mfast);
            ok = 0;
        }
    }
    if (ok) printf("LDIR replicator test: PASS (identical on both emulators, partner half copied)\n");
    return ok;
}

int main(int argc, char** argv) {
    long N = (argc > 1) ? atol(argv[1]) : N_DEFAULT;
    uint64_t rng = 0x5EEDC0FFEE123456ULL;

    long mem_match = 0, e_match = 0, steps_match = 0, halted_match = 0, all_match = 0;
    int printed = 0;

    uint8_t tape[TAPE], mref[TAPE], mfast[TAPE];

    printf("differential test: %ld random tapes, budget=%d\n", N, SB_BUDGET);
    for (long i = 0; i < N; i++) {
        gen_tape(&rng, tape);
        uint8_t d_init = (uint8_t)(sm64(&rng) & 15);

        sb_result rr = run_ref(tape, d_init, mref);
        sb_result rf = run_fast(tape, d_init, mfast);

        int m_ok = (memcmp(mref, mfast, TAPE) == 0);
        int e_ok = (rr.out_e == rf.out_e);
        int s_ok = (rr.steps == rf.steps);
        int h_ok = (rr.halted == rf.halted);
        mem_match    += m_ok;
        e_match      += e_ok;
        steps_match  += s_ok;
        halted_match += h_ok;
        all_match    += (m_ok && e_ok && s_ok && h_ok);

        if (!(m_ok && e_ok && s_ok && h_ok) && printed < 10) {
            printed++;
            printf("--- MISMATCH #%d (tape %ld, d_init=%u)\n", printed, i, d_init);
            print_tape(tape);
            printf("  ref : e=%02X steps=%d halted=%d\n", rr.out_e, rr.steps, rr.halted);
            printf("  fast: e=%02X steps=%d halted=%d\n", rf.out_e, rf.steps, rf.halted);
            if (!m_ok) {
                printf("  memory diff (idx: ref/fast):");
                for (int k = 0; k < TAPE; k++) {
                    if (mref[k] != mfast[k]) printf(" %d:%02X/%02X", k, mref[k], mfast[k]);
                }
                printf("\n");
            }
        }
    }

    printf("\nresults over %ld tapes:\n", N);
    printf("  final memory match: %ld (%.4f%%)\n", mem_match,    100.0 * (double)mem_match / (double)N);
    printf("  out_e match       : %ld (%.4f%%)\n", e_match,      100.0 * (double)e_match / (double)N);
    printf("  steps match       : %ld (%.4f%%)\n", steps_match,  100.0 * (double)steps_match / (double)N);
    printf("  halted match      : %ld (%.4f%%)\n", halted_match, 100.0 * (double)halted_match / (double)N);
    printf("  all fields match  : %ld (%.4f%%)\n", all_match,    100.0 * (double)all_match / (double)N);

    int ldir_ok = test_ldir_replicator();

    // --- throughput benchmark (single thread)
    {
        enum { POOL = 4096 };
        static uint8_t pool[POOL][TAPE];
        static uint8_t dpool[POOL];
        uint64_t brng = 0xBE9CB0A7ULL;
        for (int i = 0; i < POOL; i++) {
            gen_tape(&brng, pool[i]);
            dpool[i] = (uint8_t)(sm64(&brng) & 15);
        }
        uint8_t buf[TAPE];
        volatile uint8_t sink = 0;

        // fast emulator
        long long fast_inst = 0;
        double t0 = now_sec(), t1;
        int reps_f = 0;
        do {
            for (int i = 0; i < POOL; i++) {
                uint8_t e; int h;
                memcpy(buf, pool[i], TAPE);
                fast_inst += zf_run(buf, dpool[i], SB_BUDGET, &e, &h);
                sink ^= e;
            }
            reps_f++;
            t1 = now_sec();
        } while (t1 - t0 < 2.0);
        double fast_ips = (double)fast_inst / (t1 - t0);

        // reference emulator
        long long ref_inst = 0;
        t0 = now_sec();
        int reps_r = 0;
        do {
            for (int i = 0; i < POOL; i++) {
                z80_t cpu;
                memcpy(buf, pool[i], TAPE);
                sb_result r = sb_run(&cpu, buf, dpool[i], SB_BUDGET);
                ref_inst += r.steps;
                sink ^= r.out_e;
            }
            reps_r++;
            t1 = now_sec();
        } while (t1 - t0 < 2.0);
        double ref_ips = (double)ref_inst / (t1 - t0);

        printf("\nbenchmark (single thread, %d-tape pool, budget=%d):\n", POOL, SB_BUDGET);
        printf("  fast: %.3e instructions/sec (%d pool passes)\n", fast_ips, reps_f);
        printf("  ref : %.3e instructions/sec (%d pool passes)\n", ref_ips, reps_r);
        printf("  speedup: %.2fx\n", fast_ips / ref_ips);
        (void)sink;
    }

    double pm = 100.0 * (double)mem_match / (double)N;
    double pe = 100.0 * (double)e_match / (double)N;
    double ps = 100.0 * (double)steps_match / (double)N;
    int pass = (pm >= 99.5) && (pe >= 99.5) && (ps >= 99.0) && ldir_ok;
    printf("\noverall: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
