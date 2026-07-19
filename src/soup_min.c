// soup_min.c — de-novo origin of self-replication on the synthetic tunable VM.
//
// Same grid / pairing / mutation machinery as soup6502.c (and shares its RNG
// streams so runs are reproducible), but with the minimal VM (minvm.h) as the
// substrate and NO tasks — we study only the ORIGIN of replication, which the
// 6502 companion showed is task-independent (--notasks still ignites). The dial
// g sets the minimal-copier length L*; we measure at which epoch replicators
// ignite (functional-self-copier fraction crosses a threshold), to test
// T_origin ~ 1/rho(L*) against the density measured by density_min.
//
//   ./build/soup_min --g 8 --nb 8 --w 128 --h 128 --niches 1 --epochs 100000 \
//                    --mut 64 --seed 1 --log 200 --out results/origin_g8_s1.csv
#include "minvm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct {
    int   L, W, H; long epochs;
    double pi;
    int   mut_inv, budget, g, nb, atomic;   // atomic: replication propagates only if COMPLETE
    uint64_t seed;
    long  log_every, maxafter;   // maxafter: stop this many epochs after origin (0=off)
    const char* out_csv;
} Params;

static inline uint64_t sm64(uint64_t x){
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
typedef struct { uint64_t s; } Rng;
static inline uint64_t rnext(Rng* r){ r->s = sm64(r->s); return r->s; }
static inline double runif(Rng* r){ return (rnext(r) >> 11) * (1.0/9007199254740992.0); }
static inline uint32_t rint(Rng* r, uint32_t n){ return (uint32_t)(rnext(r) % n); }

static uint8_t* TAPES = NULL;
static int NPROG = 0, NICHE_SZ = 0;
static Params P;

static inline uint8_t* tape(int i){ return TAPES + (size_t)i*MV_L; }
static inline int niche_of(int i){ return i / NICHE_SZ; }

static inline int neighbour(int idx, int dir){
    int base = niche_of(idx) * NICHE_SZ;
    int pos  = idx - base;
    int x = pos % P.W, y = pos / P.W;
    switch(dir & 3){
        case 0: x = (x+1) % P.W; break;
        case 1: x = (x+P.W-1) % P.W; break;
        case 2: y = (y+1) % P.H; break;
        default:y = (y+P.H-1) % P.H; break;
    }
    return base + y*P.W + x;
}

static void interact(int p1, int p2){
    uint8_t m[MV_MEM];
    uint8_t orig1[MV_L];
    memcpy(orig1, tape(p1), MV_L);
    memcpy(m,      orig1,      MV_L);
    memcpy(m+MV_L, tape(p2),   MV_L);
    mv_run(m, P.g, P.nb, P.budget);
    if (P.atomic){
        // "no stepping stones": a partial copy propagates NOTHING; only a
        // complete self-copy (>=30/32) overwrites the partner. This removes the
        // incremental-assembly ladder and models an all-or-nothing (loop-like)
        // replicator whose intermediate forms are non-functional.
        int fwd = 0;
        for (int i = 0; i < MV_L; i++) if (m[MV_L + i] == orig1[i]) fwd++;
        if (fwd < 30) return;                 // nothing committed
    }
    memcpy(tape(p1), m,      MV_L);
    memcpy(tape(p2), m+MV_L, MV_L);
}

int main(int argc, char** argv){
    P.L=1; P.W=128; P.H=128; P.epochs=100000;
    P.pi=0.05; P.mut_inv=64; P.budget=64; P.g=8; P.nb=8;
    P.seed=1; P.log_every=200; P.maxafter=0; P.atomic=0; P.out_csv="results/origin_min.csv";
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--niches")) P.L=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--w")) P.W=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--h")) P.H=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--epochs")) P.epochs=atol(argv[++i]);
        else if(!strcmp(argv[i],"--pi")) P.pi=atof(argv[++i]);
        else if(!strcmp(argv[i],"--seed")) P.seed=strtoull(argv[++i],0,10);
        else if(!strcmp(argv[i],"--mut")) P.mut_inv=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--g")) P.g=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--nb")) P.nb=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--budget")) P.budget=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--log")) P.log_every=atol(argv[++i]);
        else if(!strcmp(argv[i],"--maxafter")) P.maxafter=atol(argv[++i]);
        else if(!strcmp(argv[i],"--atomic")) P.atomic=1;
        else if(!strcmp(argv[i],"--out")) P.out_csv=argv[++i];
        else { fprintf(stderr,"unknown arg %s\n",argv[i]); return 1; }
    }
    NICHE_SZ = P.W*P.H;
    NPROG = P.L * NICHE_SZ;
    TAPES = malloc((size_t)NPROG*MV_L);

    Rng gr = { sm64(P.seed ^ 0xABCDEF) };
    for(size_t i=0;i<(size_t)NPROG*MV_L;i++) TAPES[i] = (uint8_t)(rnext(&gr)>>24);

    int* order = malloc(sizeof(int)*NPROG);
    uint8_t* avail = malloc(NPROG);
    int* pair1 = malloc(sizeof(int)*NPROG);
    int* pair2 = malloc(sizeof(int)*NPROG);
    for(int i=0;i<NPROG;i++) order[i]=i;

    FILE* csv = fopen(P.out_csv,"w");
    fprintf(csv,"epoch,selected_frac,func_repl_frac,zero_frac,partial_frac,mean_copybytes,max_copybytes\n");

    int threads =
#ifdef _OPENMP
        omp_get_max_threads();
#else
        1;
#endif
    fprintf(stderr,"# soup_min: NPROG=%d (L=%d %dx%d) g=%d nb=%d L*=%d epochs=%ld mut=1/%d pi=%.3f threads=%d\n",
            NPROG,P.L,P.W,P.H,P.g,P.nb,mv_lstar(P.g),P.epochs,P.mut_inv,P.pi,threads);

    struct timespec t0; clock_gettime(CLOCK_MONOTONIC,&t0);
    long origin05 = -1, origin50 = -1;   // first epoch crossing 5% / 50%

    for(long epoch=0; epoch<=P.epochs; epoch++){
        // ---- 1. mutation (identical scheme to soup6502.c) ----
        #pragma omp parallel for schedule(static)
        for(int i=0;i<NPROG;i++){
            Rng mr = { sm64(P.seed*0x9E3779B97F4A7C15ULL + epoch*0x100000001B3ULL + (uint64_t)i*0xD1B54A32D192ED03ULL) };
            if (rint(&mr, P.mut_inv)==0){
                int pos = rint(&mr,MV_L);
                tape(i)[pos] = (uint8_t)(rnext(&mr)>>24);
            }
        }

        // ---- 2. pairing (identical to soup6502.c) ----
        Rng sr = { sm64(P.seed*0x2545F491ULL + epoch*0x100000001B3ULL) };
        for(int i=NPROG-1;i>0;i--){ int j=rint(&sr,i+1); int tmp=order[i]; order[i]=order[j]; order[j]=tmp; }
        memset(avail,1,NPROG);
        int npair=0;
        for(int k=0;k<NPROG;k++){
            int p1 = order[k];
            if(!avail[p1]) continue;
            int p2;
            if (runif(&sr) < P.pi){ p2 = rint(&sr, NPROG); }
            else { p2 = neighbour(p1, rint(&sr,4)); }
            if(p2==p1 || !avail[p2]) continue;
            avail[p1]=avail[p2]=0;
            pair1[npair]=p1; pair2[npair]=p2; npair++;
        }

        // ---- 3. interaction (no tasks: every pair interacts, p=1) ----
        #pragma omp parallel for schedule(dynamic,256)
        for(int q=0;q<npair;q++){
            interact(pair1[q], pair2[q]);
        }

        // ---- logging ----
        if (epoch % P.log_every == 0 || epoch==P.epochs){
            // scan the FULL population when it is small (no sampling noise at ignition)
            const int SAMPLE = NPROG <= 20000 ? NPROG : 4096;
            long frepl=0, zerob=0, partial=0, copysum=0, maxcopy=0;
            #pragma omp parallel reduction(+:frepl,zerob,partial,copysum) reduction(max:maxcopy)
            {
                #pragma omp for
                for(int s=0;s<SAMPLE;s++){
                    int idx = (NPROG<=SAMPLE) ? s : (int)( sm64(P.seed+epoch*777ULL+s) % NPROG );
                    const uint8_t* t = tape(idx);
                    for(int b=0;b<MV_L;b++) if(t[b]==0) zerob++;
                    int cp = mv_self_copy(t, P.g, P.nb, P.budget);
                    copysum += cp;
                    if (cp > maxcopy) maxcopy = cp;
                    if (cp >= 30) frepl++;
                    else if (cp >= 16) partial++;     // functional partial copier
                }
            }
            double sel  = (double)(2*npair)/NPROG;
            double fr   = (double)frepl/SAMPLE;
            double zf   = (double)zerob/((double)SAMPLE*MV_L);
            double pf   = (double)partial/SAMPLE;              // partial-copier fraction
            double mc   = (double)copysum/SAMPLE;             // mean self-copy bytes
            if (origin05<0 && fr>=0.05) origin05=epoch;
            if (origin50<0 && fr>=0.50) origin50=epoch;
            fprintf(csv,"%ld,%.4f,%.6f,%.4f,%.6f,%.3f,%ld\n", epoch, sel, fr, zf, pf, mc, maxcopy);
            fflush(csv);
            struct timespec t1; clock_gettime(CLOCK_MONOTONIC,&t1);
            double secs=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)*1e-9;
            fprintf(stderr,"epoch %7ld  sel=%.2f  frepl=%.5f  zero=%.2f  [%.1fs]\n",
                    epoch, sel, fr, zf, secs);
            fflush(stderr);
            // early stop: once replication has ignited and been observed for a
            // buffer of `maxafter` epochs, no need to keep running (saves time on
            // fast configs). Origin timing (t05) is already captured.
            if (P.maxafter>0 && origin05>=0 && epoch >= origin05 + P.maxafter) break;
        }
    }
    fclose(csv);
    fprintf(stderr,"# ORIGIN g=%d nb=%d L*=%d seed=%llu : t05=%ld t50=%ld (epochs; -1 = never in %ld)\n",
            P.g,P.nb,mv_lstar(P.g),(unsigned long long)P.seed,origin05,origin50,P.epochs);
    free(TAPES);free(order);free(avail);free(pair1);free(pair2);
    return 0;
}
