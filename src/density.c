// density.c — spontaneous-replicator density in RANDOM code: Z80 vs 6502.
//
// The paper's whole premise is that self-replication can assemble by chance from
// random bytes. How chance-assemblable it is depends on the instruction set: the
// Z80 has a single-instruction block move (LDIR) and a one-byte stack push chain
// (Load-Push), whereas the 6502 has neither and must build an explicit copy LOOP.
// This tool measures, over the SAME random 32-byte tapes, what fraction of them
// already copy themselves onto a fresh partner, on each CPU. It is the cleanest
// substrate comparison and is independent of any evolutionary run.
//
//   func self-copy : best of forward/reversed alignment >= 30/32 bytes
//   true replicator: gen-1 copy >= 28 AND the offspring itself copies >= 28
#define CHIPS_IMPL
#define CHIPS_ASSERT(c) ((void)0)
#include <assert.h>
#include "m6502.h"
#include "sandbox6502.h"
#include "z80fast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

static inline uint64_t sm64(uint64_t x){
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
#define BUDGET 512
static inline void partner(uint8_t* p){ for(int i=0;i<32;i++) p[i]=(uint8_t)(0x5A^(i*13)); }

// ---- Z80 copy test (fast core) ----
static int z80_copy(const uint8_t* prog, uint8_t* child){
    uint8_t m[64]; memcpy(m,prog,32); partner(m+32);
    uint8_t e; int h; zf_run(m,0,BUDGET,&e,&h);
    memcpy(child,m+32,32);
    int f=0,r=0; for(int i=0;i<32;i++){ if(m[32+i]==prog[i])f++; if(m[32+i]==prog[31-i])r++; }
    return f>r?f:r;
}
// ---- 6502 copy test ----
static int m65_copy(m6502_t* cpu,const uint8_t* prog, uint8_t* child){
    uint8_t m[64]; memcpy(m,prog,32); partner(m+32);
    sb6_run(cpu,m,0,BUDGET);
    memcpy(child,m+32,32);
    int f=0,r=0; for(int i=0;i<32;i++){ if(m[32+i]==prog[i])f++; if(m[32+i]==prog[31-i])r++; }
    return f>r?f:r;
}

int main(int argc,char**argv){
    long N = (argc>1)? atol(argv[1]) : 20000000L;
    uint64_t seed = (argc>2)? strtoull(argv[2],0,10) : 12345ULL;
    fprintf(stderr,"# sampling %ld random 32-byte tapes (seed=%llu, threads=%d)\n",
            N,(unsigned long long)seed,
#ifdef _OPENMP
            omp_get_max_threads()
#else
            1
#endif
    );
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC,&t0);

    long z_fc=0, z_tr=0, m_fc=0, m_tr=0;
    #pragma omp parallel reduction(+:z_fc,z_tr,m_fc,m_tr)
    {
        m6502_t cpu;
        #pragma omp for schedule(dynamic,65536)
        for(long i=0;i<N;i++){
            uint8_t prog[32];
            uint64_t s = sm64(seed + (uint64_t)i*0x9E3779B97F4A7C15ULL);
            for(int b=0;b<32;b++){ s=sm64(s); prog[b]=(uint8_t)(s>>24); }
            uint8_t child[32], grand[32];
            // Z80
            int zc = z80_copy(prog, child);
            if(zc>=30) z_fc++;
            if(zc>=28 && z80_copy(child,grand)>=28) z_tr++;
            // 6502
            int mc = m65_copy(&cpu, prog, child);
            if(mc>=30) m_fc++;
            if(mc>=28 && m65_copy(&cpu, child,grand)>=28) m_tr++;
        }
    }
    struct timespec t1; clock_gettime(CLOCK_MONOTONIC,&t1);
    double secs=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)*1e-9;

    printf("random-code spontaneous replicator density (N=%ld tapes)\n\n", N);
    printf("             func self-copy (>=30/32)         true replicator (recursive)\n");
    printf("  Z80    %8ld   %.2e per tape        %8ld   %.2e per tape\n",
           z_fc,(double)z_fc/N, z_tr,(double)z_tr/N);
    printf("  6502   %8ld   %.2e per tape        %8ld   %.2e per tape\n",
           m_fc,(double)m_fc/N, m_tr,(double)m_tr/N);
    if(m_fc>0) printf("\n  Z80/6502 func-copy density ratio: %.1fx\n",(double)z_fc/m_fc);
    else       printf("\n  6502 func-copy density: 0 in %ld  (< %.2e)\n", N, 1.0/N);
    if(m_tr>0) printf("  Z80/6502 true-repl density ratio: %.1fx\n",(double)z_tr/m_tr);
    else       printf("  6502 true-repl density: 0 in %ld  (< %.2e)\n", N, 1.0/N);
    fprintf(stderr,"# %.1fs\n", secs);
    return 0;
}
