// soup6502.c — Digital primordial soup of MOS 6502 programs (Algorithm 1).
//
// The "different CPU" companion to soup.c. Same model as Cicala et al.
// (arXiv:2607.09211) — a grid of niches of 32-byte programs; self-replication is
// never issued as a command but must emerge from random bytes; task-solving
// (polynomial evaluation) co-evolves under competence-gated interaction — but the
// substrate is the accumulator-based 6502 instead of the register-rich Z80.
//
// The grid/pairing/mutation/metric machinery is identical to soup.c (and shares
// its RNG streams, so pairings are reproducible). Only the CPU-specific parts
// differ: the execution sandbox (sandbox6502.h), the register mapping
// (input in X, output in A), the "halt" opcode (JAM), the replicator byte
// signatures, and the planted genotypes.
//
// Key substrate difference: the 6502 has NO block-move instruction. A replicator
// must build an explicit copy LOOP (indexed store, or read-backward stack push),
// which is ~4x more instructions than the Z80's one-instruction LDIR and far
// rarer to assemble by chance.
#define CHIPS_IMPL
#define CHIPS_ASSERT(c) ((void)0)
#include <assert.h>
#include "m6502.h"
#include "sandbox6502.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

// ------------------------- parameters (defaults from Table 1) ---------------
typedef struct {
    int   L; int W, H; long epochs;
    double p_base, p_succ, C, pi;
    int   mut_inv, budget;
    uint64_t seed;
    long  log_every;
    int   single_task;
    int   plant, plant_solver, plant_dud;
    int   notasks;
    const char* out_csv;
    const char* dump;
} Params;

// --------------------------- polynomial tasks (same as soup.c) --------------
typedef struct { int c0,c1,c2,c3; const char* name; } Poly;
static const Poly TASKS[32] = {
    {0,1,0,0,"n"}, {1,1,0,0,"n+1"}, {2,1,0,0,"n+2"}, {3,1,0,0,"n+3"},
    {-1,1,0,0,"n-1"}, {-2,1,0,0,"n-2"}, {0,-1,0,0,"-n"}, {0,2,0,0,"2n"},
    {0,3,0,0,"3n"}, {0,4,0,0,"4n"}, {1,2,0,0,"2n+1"}, {3,-2,0,0,"-2n+3"},
    {1,3,0,0,"3n+1"}, {7,3,0,0,"3n+7"}, {2,5,0,0,"5n+2"}, {3,5,0,0,"5n+3"},
    {-2,5,0,0,"5n-2"}, {7,11,0,0,"11n+7"},
    {0,0,1,0,"n^2"}, {2,0,-1,0,"-n^2+2"}, {0,2,1,0,"n^2+2n"}, {1,2,1,0,"n^2+2n+1"},
    {1,3,1,0,"n^2+3n+1"}, {0,-1,1,0,"n^2-n"}, {0,1,2,0,"2n^2+n"}, {1,2,-1,0,"-n^2+2n+1"},
    {1,1,1,0,"n^2+n+1"},
    {0,0,0,1,"n^3"}, {0,1,1,1,"n^3+n^2+n"}, {3,1,1,1,"n^3+n^2+n+3"},
    {0,1,0,1,"n^3+n"}, {1,0,0,1,"n^3+1"},
};
static inline uint8_t poly_eval(int t, int n){
    const Poly* p=&TASKS[t];
    long v=p->c0+p->c1*(long)n+p->c2*(long)n*n+p->c3*(long)n*n*n;
    return (uint8_t)(((v%256)+256)%256);
}

// ------------------------------- RNG (same as soup.c) -----------------------
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

// ------------------------------- globals ------------------------------------
static uint8_t* TAPES = NULL;
static int NPROG = 0, NICHE_SZ = 0;
static Params P;

static inline sb6_result RUN(m6502_t* cpu, uint8_t* m, uint8_t x, int b){
    return sb6_run(cpu, m, x, b);
}
static inline uint8_t* tape(int i){ return TAPES + (size_t)i*32; }
static inline int niche_of(int i){ return i / NICHE_SZ; }
static inline int task_of(int i){ return (P.L==1) ? P.single_task : niche_of(i); }

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

// ------------------------- validation + interaction -------------------------
static int validate(m6502_t* cpu, int p1, Rng* r, double* avg_k){
    int task = task_of(p1);
    int in[3]; int cnt=0;
    while (cnt < 3){
        int c = rint(r, 16); int dup=0;
        for (int j=0;j<cnt;j++) if(in[j]==c) dup=1;
        if(!dup) in[cnt++]=c;
    }
    long ksum=0; int runs=0; int ok=1;
    uint8_t m[SB6_MEM];
    for (int i=0;i<3;i++){
        memcpy(m, tape(p1), 32);
        memset(m+32, 0, 32);
        sb6_result res = RUN(cpu, m, (uint8_t)in[i], P.budget);
        ksum += res.steps; runs++;
        if (res.out_a != poly_eval(task, in[i])) { ok=0; break; }
    }
    *avg_k = (double)ksum / runs;
    return ok;
}

static void interact(m6502_t* cpu, int p1, int p2){
    uint8_t m[SB6_MEM];
    memcpy(m,    tape(p1), 32);
    memcpy(m+32, tape(p2), 32);
    RUN(cpu, m, 0, P.budget);
    memcpy(tape(p1), m,    32);
    memcpy(tape(p2), m+32, 32);
}

// ------------------------------ metrics -------------------------------------
static int contains1(const uint8_t* t, uint8_t b){
    for(int i=0;i<32;i++) if(t[i]==b) return 1; return 0;
}
// Coarse architecture proxies (real classification is by disassembly in
// analyze6502.c). "store-loop" copy uses an indexed store; "stack" copy uses PHA.
static int has_storeloop(const uint8_t* t){
    return contains1(t,0x95) || contains1(t,0x9D) || contains1(t,0x99) || contains1(t,0x81) || contains1(t,0x91);
}
static int has_stackcopy(const uint8_t* t){ return contains1(t,0x48); }  // PHA

static int self_copy_bytes(m6502_t* cpu, const uint8_t* prog, int* steps, int* halted){
    uint8_t m[SB6_MEM];
    memcpy(m, prog, 32);
    for(int i=0;i<32;i++) m[32+i] = (uint8_t)(0x5A ^ (i*13));   // non-zero partner
    sb6_result r = RUN(cpu, m, 0, P.budget);
    *steps = r.steps; *halted = r.halted;
    int fwd=0, rev=0;
    for(int i=0;i<32;i++){ if(m[32+i]==prog[i]) fwd++; if(m[32+i]==prog[31-i]) rev++; }
    return fwd>rev?fwd:rev;
}
static int solves_all(m6502_t* cpu, const uint8_t* prog, int task){
    uint8_t m[SB6_MEM];
    for(int n=0;n<16;n++){
        memcpy(m, prog, 32); memset(m+32,0,32);
        sb6_result r = RUN(cpu, m, (uint8_t)n, P.budget);
        if (r.out_a != poly_eval(task, n)) return 0;
    }
    return 1;
}

// ------------------------------- main loop ----------------------------------
int main(int argc, char** argv){
    P.L=1; P.W=128; P.H=128; P.epochs=30000;
    P.p_base=0.3; P.p_succ=1.0; P.C=0.3; P.pi=0.05;
    P.mut_inv=64; P.budget=512; P.seed=1; P.log_every=100;
    P.single_task=10; P.plant=0; P.plant_solver=0; P.plant_dud=0;
    P.notasks=0; P.out_csv="results/run6502.csv"; P.dump=NULL;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--niches")) P.L=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--w")) P.W=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--h")) P.H=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--epochs")) P.epochs=atol(argv[++i]);
        else if(!strcmp(argv[i],"--C")) P.C=atof(argv[++i]);
        else if(!strcmp(argv[i],"--pi")) P.pi=atof(argv[++i]);
        else if(!strcmp(argv[i],"--seed")) P.seed=strtoull(argv[++i],0,10);
        else if(!strcmp(argv[i],"--task")) P.single_task=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--plant")) P.plant=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--plant_solver")) P.plant_solver=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--plant_dud")) P.plant_dud=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--notasks")) P.notasks=1;
        else if(!strcmp(argv[i],"--dump")) P.dump=argv[++i];
        else if(!strcmp(argv[i],"--log")) P.log_every=atol(argv[++i]);
        else if(!strcmp(argv[i],"--out")) P.out_csv=argv[++i];
        else if(!strcmp(argv[i],"--budget")) P.budget=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--mut")) P.mut_inv=atoi(argv[++i]);
        else { fprintf(stderr,"unknown arg %s\n",argv[i]); return 1; }
    }
    NICHE_SZ = P.W*P.H;
    NPROG = P.L * NICHE_SZ;
    TAPES = malloc((size_t)NPROG*32);

    Rng gr = { sm64(P.seed ^ 0xABCDEF) };
    for(size_t i=0;i<(size_t)NPROG*32;i++) TAPES[i] = (uint8_t)(rnext(&gr)>>24);

    // Canonical 6502 replicator: indexed-store copy loop (verified in
    // test_genotype6502). LDX #$1F; LDA $00,X; STA $20,X; DEX; BPL; JAM
    if (P.plant>0){
        uint8_t rep[32]; memset(rep,0,32);
        uint8_t c[]={0xA2,0x1F,0xB5,0x00,0x95,0x20,0xCA,0x10,0xF9,0x02};
        memcpy(rep,c,sizeof c);
        for(int k=0;k<P.plant;k++){ int idx=rint(&gr,NPROG); memcpy(tape(idx),rep,32); }
        fprintf(stderr,"# planted %d indexed-store copy-loop replicators\n", P.plant);
    }
    // Matched competence-gating genotypes. Both use the same indexed-store copy
    // loop; the result is carried in Y across the loop (which clobbers A and X).
    //  solver+replicator: TXA;ASL;ORA #1;TAY; [copy loop]; TYA; JAM  (solves 2n+1)
    //  non-solving dud  : NOP*4;   TAY;       [copy loop]; NOP; JAM  (copies only)
    if (P.plant_solver>0){
        uint8_t g[32]; memset(g,0,32);
        uint8_t c[]={0x8A,0x0A,0x09,0x01,0xA8,0xA2,0x1F,0xB5,0x00,0x95,0x20,0xCA,0x10,0xF9,0x98,0x02};
        memcpy(g,c,sizeof c);
        for(int k=0;k<P.plant_solver;k++){ int idx=rint(&gr,NPROG); memcpy(tape(idx),g,32); }
        fprintf(stderr,"# planted %d solver+replicators (solve 2n+1 AND copy)\n", P.plant_solver);
    }
    if (P.plant_dud>0){
        uint8_t g[32]; memset(g,0,32);
        uint8_t c[]={0xEA,0xEA,0xEA,0xEA,0xA8,0xA2,0x1F,0xB5,0x00,0x95,0x20,0xCA,0x10,0xF9,0xEA,0x02};
        memcpy(g,c,sizeof c);
        for(int k=0;k<P.plant_dud;k++){ int idx=rint(&gr,NPROG); memcpy(tape(idx),g,32); }
        fprintf(stderr,"# planted %d non-solving replicators (copy only)\n", P.plant_dud);
    }

    int* order = malloc(sizeof(int)*NPROG);
    uint8_t* avail = malloc(NPROG);
    int* pair1 = malloc(sizeof(int)*NPROG);
    int* pair2 = malloc(sizeof(int)*NPROG);
    for(int i=0;i<NPROG;i++) order[i]=i;

    FILE* csv = fopen(P.out_csv,"w");
    fprintf(csv,"epoch,selected_frac,copy_sig_frac,storeloop_frac,stackcopy_frac,"
                "func_repl_frac,true_repl_frac,solve_frac,niches_solved,avg_steps,halt_frac,uniq_frac,zero_frac\n");

    fprintf(stderr,"# soup6502: NPROG=%d (L=%d %dx%d) epochs=%ld C=%.2f pi=%.3f budget=%d threads=%d\n",
            NPROG, P.L, P.W, P.H, P.epochs, P.C, P.pi, P.budget,
#ifdef _OPENMP
            omp_get_max_threads()
#else
            1
#endif
    );

    struct timespec t0; clock_gettime(CLOCK_MONOTONIC,&t0);

    for(long epoch=0; epoch<=P.epochs; epoch++){
        // ---- 1. mutation (same RNG scheme as soup.c) ----
        #pragma omp parallel for schedule(static)
        for(int i=0;i<NPROG;i++){
            Rng mr = { sm64(P.seed*0x9E3779B97F4A7C15ULL + epoch*0x100000001B3ULL + (uint64_t)i*0xD1B54A32D192ED03ULL) };
            if (rint(&mr, P.mut_inv)==0){
                int pos = rint(&mr,32);
                tape(i)[pos] = (uint8_t)(rnext(&mr)>>24);
            }
        }

        // ---- 2. pairing (same as soup.c) ----
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

        // ---- 3+4. validation + interaction ----
        #pragma omp parallel
        {
            m6502_t cpu;
            #pragma omp for schedule(dynamic,256)
            for(int q=0;q<npair;q++){
                int p1=pair1[q], p2=pair2[q];
                Rng pr = { sm64(P.seed*0xD1B54A32D192ED03ULL + epoch*0x9E3779B1ULL + (uint64_t)q*0x100000001B3ULL) };
                double p;
                if (P.notasks){ p = 1.0; }
                else {
                    double avg_k=0;
                    int validated = validate(&cpu, p1, &pr, &avg_k);
                    p = validated ? (P.p_succ - P.C*avg_k/P.budget) : P.p_base;
                    if(p<0) p=0;
                }
                if (runif(&pr) < p) interact(&cpu, p1, p2);
            }
        }

        // ---- logging ----
        if (epoch % P.log_every == 0 || epoch==P.epochs){
            const int SAMPLE = NPROG < 4096 ? NPROG : 4096;
            long sig=0, sl=0, sc=0, frepl=0, trepl=0, solve=0, hlt=0, stp=0, zerob=0;
            #pragma omp parallel reduction(+:sig,sl,sc,frepl,trepl,solve,hlt,stp,zerob)
            {
                m6502_t cpu;
                #pragma omp for
                for(int s=0;s<SAMPLE;s++){
                    int idx = (NPROG<=SAMPLE) ? s : (int)( sm64(P.seed+epoch*777ULL+s) % NPROG );
                    const uint8_t* t = tape(idx);
                    int isl = has_storeloop(t), ip = has_stackcopy(t);
                    if(isl) sl++;
                    if(ip) sc++;
                    if(isl||ip) sig++;
                    for(int b=0;b<32;b++) if(t[b]==0) zerob++;
                    int steps=0, halted=0;
                    int cp = self_copy_bytes(&cpu, t, &steps, &halted);
                    if(cp >= 30){ frepl++;
                        uint8_t m[SB6_MEM]; memcpy(m,t,32);
                        for(int b=0;b<32;b++) m[32+b]=(uint8_t)(0x5A^(b*13));
                        RUN(&cpu,m,0,P.budget);
                        uint8_t child[32]; memcpy(child,m+32,32);
                        for(int b=0;b<32;b++) m[b]=child[b];
                        for(int b=0;b<32;b++) m[32+b]=(uint8_t)(0x5A^(b*13));
                        RUN(&cpu,m,0,P.budget);
                        int fwd=0,rev=0; for(int b=0;b<32;b++){if(m[32+b]==child[b])fwd++;if(m[32+b]==child[31-b])rev++;}
                        if((fwd>rev?fwd:rev)>=28) trepl++;
                    }
                    stp += steps;
                    if(halted) hlt++;
                    if(solves_all(&cpu, t, task_of(idx))) solve++;
                }
            }
            int niches_solved=0;
            #pragma omp parallel for reduction(+:niches_solved)
            for(int nch=0;nch<P.L;nch++){
                m6502_t cpu; int base=nch*NICHE_SZ; int task=(P.L==1)?P.single_task:nch;
                int chk = NICHE_SZ<1024?NICHE_SZ:1024; long good=0;
                for(int s=0;s<chk;s++){
                    int idx = base + (NICHE_SZ<=chk? s : (int)(sm64((uint64_t)nch*131ULL+epoch+s)%NICHE_SZ));
                    if(solves_all(&cpu, tape(idx), task)) good++;
                }
                if ((double)good/chk >= 0.10) niches_solved++;
            }
            double uniq;
            {
                Rng ur={sm64(P.seed+epoch*99ULL+7)}; long diff=0,tot=0;
                for(int s=0;s<512;s++){
                    int a=rint(&ur,NPROG), b=rint(&ur,NPROG);
                    uint64_t ha=1469598103934665603ULL, hb=ha;
                    for(int j=0;j<32;j++){ha=(ha^tape(a)[j])*1099511628211ULL; hb=(hb^tape(b)[j])*1099511628211ULL;}
                    if(ha!=hb) diff++; tot++;
                }
                uniq = tot? (double)diff/tot : 0;
            }

            struct timespec t1; clock_gettime(CLOCK_MONOTONIC,&t1);
            double secs=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)*1e-9;
            double sel = (double)(2*npair)/NPROG;
            double avg_steps = (double)stp/SAMPLE;
            double zero_frac = (double)zerob/((double)SAMPLE*32);
            fprintf(csv,"%ld,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.1f,%.4f,%.4f,%.4f\n",
                epoch, sel,
                (double)sig/SAMPLE,(double)sl/SAMPLE,(double)sc/SAMPLE,
                (double)frepl/SAMPLE,(double)trepl/SAMPLE,(double)solve/SAMPLE, niches_solved,
                avg_steps, (double)hlt/SAMPLE, uniq, zero_frac);
            fflush(csv);
            fprintf(stderr,"epoch %7ld  sel=%.2f  copy_sig=%.3f frepl=%.4f trepl=%.4f  solve=%.4f n=%d/%d  avgk=%.0f zero=%.2f  [%.1fs]\n",
                epoch, sel, (double)sig/SAMPLE, (double)frepl/SAMPLE, (double)trepl/SAMPLE,
                (double)solve/SAMPLE, niches_solved, P.L, avg_steps, zero_frac, secs);
            fflush(stderr);
        }
    }
    fclose(csv);
    if (P.dump){
        FILE* df = fopen(P.dump,"wb");
        if(df){ fwrite(TAPES,1,(size_t)NPROG*32,df); fclose(df);
                fprintf(stderr,"# dumped %d tapes (%zu bytes) to %s\n",NPROG,(size_t)NPROG*32,P.dump); }
    }
    free(TAPES);free(order);free(avail);free(pair1);free(pair2);
    return 0;
}
