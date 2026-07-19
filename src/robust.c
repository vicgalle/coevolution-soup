// robust.c — mutational robustness of the canonical replicator: Z80 vs 6502.
//
// The paper's Fig 2F measures how often a replicator still reproduces after k
// random byte mutations. We do exactly that for the two substrates' compact
// canonical replicators, over many random mutation draws:
//   Z80 :  LD E,$20 ; LD C,E ; LDIR              (1E 20 4B ED B0)     5 machinery bytes
//   6502:  LDX #$1F ; LDA $00,X ; STA $20,X ; DEX ; BPL ; JAM        10 machinery bytes
// The 6502 has no block move, so its copy is a loop; it also NEEDS an explicit
// halt, because a trailing 0x00 is BRK (destructive) on the 6502 but NOP (inert)
// on the Z80. Both effects double the machinery = double the mutational target,
// which this quantifies directly and seed-robustly.
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

static int z80_copies(const uint8_t* prog){
    uint8_t m[64]; memcpy(m,prog,32); partner(m+32);
    uint8_t e; int h; zf_run(m,0,BUDGET,&e,&h);
    int f=0,r=0; for(int i=0;i<32;i++){ if(m[32+i]==prog[i])f++; if(m[32+i]==prog[31-i])r++; }
    return (f>r?f:r) >= 30;
}
static int m65_copies(m6502_t* cpu,const uint8_t* prog){
    uint8_t m[64]; memcpy(m,prog,32); partner(m+32);
    sb6_run(cpu,m,0,BUDGET);
    int f=0,r=0; for(int i=0;i<32;i++){ if(m[32+i]==prog[i])f++; if(m[32+i]==prog[31-i])r++; }
    return (f>r?f:r) >= 30;
}

int main(int argc,char**argv){
    long T = (argc>1)? atol(argv[1]) : 200000L;   // trials per (cpu,k)
    // canonical compact replicators (rest of tape zero)
    uint8_t z0[32]={0}; uint8_t zc[]={0x1E,0x20,0x4B,0xED,0xB0}; memcpy(z0,zc,sizeof zc);
    uint8_t m0[32]={0}; uint8_t mc[]={0xA2,0x1F,0xB5,0x00,0x95,0x20,0xCA,0x10,0xF9,0x02}; memcpy(m0,mc,sizeof mc);

    // sanity: unmutated must copy
    { m6502_t cpu; if(!z80_copies(z0)||!m65_copies(&cpu,m0)){ fprintf(stderr,"canonical replicator does not copy!\n"); return 1; } }

    const int KS[4]={1,2,4,8};
    printf("mutational robustness (fraction still self-copying after k byte mutations, T=%ld)\n\n",T);
    printf("   k mutations        Z80          6502\n");
    for(int ki=0;ki<4;ki++){
        int k=KS[ki];
        long zsurv=0, msurv=0;
        #pragma omp parallel reduction(+:zsurv,msurv)
        {
            m6502_t cpu;
            #pragma omp for schedule(dynamic,4096)
            for(long t=0;t<T;t++){
                uint64_t s = sm64(0xABCD*(uint64_t)(ki+1) + (uint64_t)t*0x9E3779B97F4A7C15ULL);
                // pick k distinct positions, mutate each to a random byte, on both tapes
                uint8_t zt[32], mt[32]; memcpy(zt,z0,32); memcpy(mt,m0,32);
                int pos[8]; int np=0;
                while(np<k){ s=sm64(s); int p=(int)(s%32); int dup=0; for(int j=0;j<np;j++) if(pos[j]==p)dup=1; if(!dup)pos[np++]=p; }
                for(int j=0;j<k;j++){ s=sm64(s); uint8_t v=(uint8_t)(s>>24); zt[pos[j]]=v; mt[pos[j]]=v; }
                if(z80_copies(zt)) zsurv++;
                if(m65_copies(&cpu,mt)) msurv++;
            }
        }
        printf("   %2d           %6.2f%%       %6.2f%%\n", k, 100.0*zsurv/T, 100.0*msurv/T);
    }
    return 0;
}
