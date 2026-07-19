// density_min.c — spontaneous self-copier density in random code on the synthetic
// tunable VM (minvm.h), as a function of the two dials g (copy granularity ->
// minimal-copier length L*) and NB (opcode dilution -> per-op probability 1/NB).
//
// This is the controlled analog of density.c (which measured Z80 vs 6502 over the
// same random tapes). Here, instead of two fixed CPUs, we sweep L* smoothly and
// measure how fast the replicator density falls. Prediction: log(rho) is linear
// in L* with slope ~ -log(NB), i.e. each extra COPY instruction that must line up
// by chance costs a factor ~1/NB.
//
//   build: clang -O3 -march=native -Xpreprocessor -fopenmp -I$LIBOMP/include \
//                -L$LIBOMP/lib -lomp -o build/density_min src/density_min.c
//   run:   ./build/density_min 1000000000 12345 > results/density_min.csv
#include "minvm.h"
#include <stdio.h>
#include <stdlib.h>
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
#define BUDGET 64

static int parse_list(const char* s, int* out, int max){
    int n=0; char buf[256]; strncpy(buf,s,255); buf[255]=0;
    char* tok=strtok(buf,","); while(tok && n<max){ out[n++]=atoi(tok); tok=strtok(NULL,","); }
    return n;
}

int main(int argc, char** argv){
    long N       = (argc>1)? atol(argv[1]) : 200000000L;
    uint64_t sd  = (argc>2)? strtoull(argv[2],0,10) : 12345ULL;

    // Dials to sweep: g (divisors of 32) and NB (opcode-bucket count).
    // Optional argv[3]=nb list, argv[4]=g list (comma-separated) for targeted runs.
    int gs[16]  = {32,16,8,4,2};   int NG  = 5;
    int nbs[16] = {8,5,6};         int NNB = 3;
    if (argc>3) NNB = parse_list(argv[3], nbs, 16);
    if (argc>4) NG  = parse_list(argv[4], gs,  16);

    int threads =
#ifdef _OPENMP
        omp_get_max_threads();
#else
        1;
#endif
    fprintf(stderr,"# density_min: N=%ld tapes/config, seed=%llu, threads=%d\n",
            N,(unsigned long long)sd,threads);

    printf("nb,g,lstar,minbytes,N,func_count,density\n");

    for (int ni=0; ni<NNB; ni++){
        int nb = nbs[ni];
        for (int gi=0; gi<NG; gi++){
            int g = gs[gi];
            int lstar = mv_lstar(g);
            struct timespec t0; clock_gettime(CLOCK_MONOTONIC,&t0);
            long fc = 0;
            #pragma omp parallel reduction(+:fc)
            {
                #pragma omp for schedule(dynamic,262144)
                for (long i=0;i<N;i++){
                    uint8_t prog[MV_L];
                    uint64_t s = sm64(sd ^ ((uint64_t)nb*0x1000003ULL) ^ ((uint64_t)g*0x9E37ULL)
                                         ^ ((uint64_t)i*0x9E3779B97F4A7C15ULL));
                    for (int b=0;b<MV_L;b++){ s=sm64(s); prog[b]=(uint8_t)(s>>24); }
                    if (mv_self_copy(prog,g,nb,BUDGET) >= 30) fc++;
                }
            }
            struct timespec t1; clock_gettime(CLOCK_MONOTONIC,&t1);
            double secs=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)*1e-9;
            double dens = (double)fc/(double)N;
            printf("%d,%d,%d,%d,%ld,%ld,%.6e\n", nb,g,lstar,1+MV_L/g,N,fc,dens);
            fflush(stdout);
            fprintf(stderr,"  nb=%d g=%2d  L*=%2d  copiers=%9ld  rho=%.3e   [%.1fs]\n",
                    nb,g,lstar,fc,dens,secs);
        }
    }
    return 0;
}
