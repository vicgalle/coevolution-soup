// soup.c — Digital primordial soup of Z80 programs (Algorithm 1).
//
// Replicates Cicala et al., "Co-evolution of self-replication and function in a
// digital primordial soup" (arXiv:2607.09211), Algorithm 1 + Methods 4.1-4.4.
//
// Grid of L niches, each a WxH torus of 32-byte Z80 programs. Each epoch:
//   1. mutate each program with prob 1/64 (one random byte -> random value)
//   2. pair available programs with an available von Neumann neighbour
//      (with prob pi the partner is drawn globally = cross-niche pollination)
//   3. validate P1 on its niche polynomial (3 sampled inputs); interaction prob
//      is p_succ - C*k/B if validated else p_base
//   4. with that prob, execute concat(P1,P2) for B=512 insns and split back
//
// Replication is never imposed: it only happens if executed code copies bytes.
//
// Design for speed: the pairing step (sequential, cheap) produces a list of
// disjoint (P1,P2) pairs; the expensive Z80 executions run in an OpenMP
// parallel-for over that list, each pair using a deterministic per-pair RNG so
// results are reproducible regardless of thread scheduling.
#define CHIPS_IMPL
#include "z80.h"
#include "sandbox.h"
#include "z80fast.h"
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
    int   L;            // number of niches
    int   W, H;         // niche grid dimensions
    long  epochs;
    double p_base;      // 0.3
    double p_succ;      // 1.0
    double C;           // metabolic penalty coefficient (default 0.3)
    double pi;          // cross-niche pollination (0.05)
    int   mut_inv;      // mutation rate denominator (64 -> 1/64)
    int   budget;       // instruction budget B (512)
    uint64_t seed;
    long  log_every;
    int   single_task;  // if L==1, which task index to use
    int   plant;        // plant this many canonical LDIR replicators at init (diagnostic)
    int   plant_solver; // plant this many solver+replicator genotypes (2n+1 + bounded LDIR)
    int   plant_dud;    // plant this many matched NON-solving replicators (same LDIR copy)
    int   notasks;      // 1 = skip validation, always interact (Fig 2D control)
    int   anim;         // if >0, emit an animation frame every `anim` epochs (denser early)
    const char* animout;// animation frame file
    const char* out_csv;
    const char* dump;   // optional: write final population (NPROG*32 bytes) here
} Params;

// --------------------------- polynomial tasks -------------------------------
// value = (c0 + c1*n + c2*n^2 + c3*n^3) mod 256
typedef struct { int c0,c1,c2,c3; const char* name; } Poly;
static const Poly TASKS[32] = {
    // 18 linear
    {0,1,0,0,"n"}, {1,1,0,0,"n+1"}, {2,1,0,0,"n+2"}, {3,1,0,0,"n+3"},
    {-1,1,0,0,"n-1"}, {-2,1,0,0,"n-2"}, {0,-1,0,0,"-n"}, {0,2,0,0,"2n"},
    {0,3,0,0,"3n"}, {0,4,0,0,"4n"}, {1,2,0,0,"2n+1"}, {3,-2,0,0,"-2n+3"},
    {1,3,0,0,"3n+1"}, {7,3,0,0,"3n+7"}, {2,5,0,0,"5n+2"}, {3,5,0,0,"5n+3"},
    {-2,5,0,0,"5n-2"}, {7,11,0,0,"11n+7"},
    // 9 quadratic
    {0,0,1,0,"n^2"}, {2,0,-1,0,"-n^2+2"}, {0,2,1,0,"n^2+2n"}, {1,2,1,0,"n^2+2n+1"},
    {1,3,1,0,"n^2+3n+1"}, {0,-1,1,0,"n^2-n"}, {0,1,2,0,"2n^2+n"}, {1,2,-1,0,"-n^2+2n+1"},
    {1,1,1,0,"n^2+n+1"},
    // 5 cubic
    {0,0,0,1,"n^3"}, {0,1,1,1,"n^3+n^2+n"}, {3,1,1,1,"n^3+n^2+n+3"},
    {0,1,0,1,"n^3+n"}, {1,0,0,1,"n^3+1"},
};
static inline uint8_t poly_eval(int t, int n) {
    const Poly* p = &TASKS[t];
    long v = p->c0 + p->c1*(long)n + p->c2*(long)n*n + p->c3*(long)n*n*n;
    return (uint8_t)(((v % 256) + 256) % 256);
}

// ------------------------------- RNG ----------------------------------------
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
static uint8_t* TAPES = NULL;   // NPROG * 32
static int NPROG = 0, NICHE_SZ = 0;
static Params P;

// Unified execution: by default use the fast instruction-stepped core (zf_run),
// which is differential-tested to match chips 100% on final memory/E/steps/halted
// over 3M random tapes at ~6x the speed. Build with -DUSE_CHIPS to use chips.
#ifdef USE_CHIPS
  #define RUN(cpu,m,d,b) sb_run(cpu,m,d,b)
#else
  static inline sb_result RUN(z80_t* cpu, uint8_t* m, uint8_t d, int b){
      (void)cpu; sb_result r; int h=0; r.steps=zf_run(m,d,b,&r.out_e,&h); r.halted=h; return r;
  }
#endif

static inline uint8_t* tape(int i){ return TAPES + (size_t)i*32; }
static inline int niche_of(int i){ return i / NICHE_SZ; }
static inline int task_of(int i){ return (P.L==1) ? P.single_task : niche_of(i); }

// von Neumann neighbour: dir 0..3 -> (x+1,y),(x-1,y),(x,y+1),(x,y-1), toroidal
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
// Validate P1: run on [P1|zeros] with D=input for up to 3 distinct inputs.
// Short-circuits on first wrong output. Returns 1 if all 3 correct, sets *avg_k.
static int validate(z80_t* cpu, int p1, Rng* r, double* avg_k){
    int task = task_of(p1);
    // draw 3 distinct inputs from {0..15} without replacement
    int in[3]; int cnt=0;
    while (cnt < 3){
        int c = rint(r, 16); int dup=0;
        for (int j=0;j<cnt;j++) if(in[j]==c) dup=1;
        if(!dup) in[cnt++]=c;
    }
    long ksum=0; int runs=0; int ok=1;
    uint8_t m[SB_MEM];
    for (int i=0;i<3;i++){
        memcpy(m, tape(p1), 32);
        memset(m+32, 0, 32);
        sb_result res = RUN(cpu, m, (uint8_t)in[i], P.budget);
        ksum += res.steps; runs++;
        if (res.out_e != poly_eval(task, in[i])) { ok=0; break; }
    }
    *avg_k = (double)ksum / runs;
    return ok;
}

// Execute interaction concat(P1,P2), D=0, split back into the two tapes.
static void interact(z80_t* cpu, int p1, int p2){
    uint8_t m[SB_MEM];
    memcpy(m,    tape(p1), 32);
    memcpy(m+32, tape(p2), 32);
    RUN(cpu, m, 0, P.budget);
    memcpy(tape(p1), m,    32);
    memcpy(tape(p2), m+32, 32);
}

// ------------------------------ metrics -------------------------------------
static int contains(const uint8_t* t, const uint8_t* pat, int plen){
    for (int i=0;i+plen<=32;i++){
        int ok=1; for(int j=0;j<plen;j++) if(t[i+j]!=pat[j]){ok=0;break;}
        if(ok) return 1;
    }
    return 0;
}
// LDIR-family and Load-Push signatures (Methods 4.4)
static int is_ldir_family(const uint8_t* t){
    static const uint8_t s[4][2] = {{0xED,0xB0},{0xED,0xB8},{0xED,0xA0},{0xED,0xA8}};
    for(int k=0;k<4;k++) if(contains(t,s[k],2)) return 1; return 0;
}
static int is_loadpush_family(const uint8_t* t){
    static const uint8_t s[4][2] = {{0x01,0xC5},{0x11,0xD5},{0x21,0xE5},{0xE5,0x2A}};
    for(int k=0;k<4;k++) if(contains(t,s[k],2)) return 1; return 0;
}
// Functional self-replication: run the program (D=0) against a blank partner,
// like an interaction. Reports how many of its 32 bytes it copied into the
// partner half, plus execution length and whether it halted.
static int self_copy_bytes(z80_t* cpu, const uint8_t* prog, int* steps, int* halted){
    uint8_t m[SB_MEM];
    memcpy(m, prog, 32);
    // Non-zero partner: a trivial all-NOP (zero) tape leaves this untouched and
    // is NOT counted as a replicator; only a program that actively overwrites the
    // partner with its own bytes counts. (Paper uses a zero partner for its hand-
    // seeded replicators; a non-zero partner is needed to screen a population.)
    for(int i=0;i<32;i++) m[32+i] = (uint8_t)(0x5A ^ (i*13));
    sb_result r = RUN(cpu, m, 0, P.budget);
    *steps = r.steps; *halted = r.halted;
    // Direction-agnostic: LDIR copies forward, Load-Push copies reversed onto the
    // stack. Count the best of the two alignments.
    int fwd=0, rev=0;
    for(int i=0;i<32;i++){ if(m[32+i]==prog[i]) fwd++; if(m[32+i]==prog[31-i]) rev++; }
    return fwd>rev?fwd:rev;
}
// Does the program solve its task on all 16 inputs?
static int solves_all(z80_t* cpu, const uint8_t* prog, int task){
    uint8_t m[SB_MEM];
    for(int n=0;n<16;n++){
        memcpy(m, prog, 32); memset(m+32,0,32);
        sb_result r = RUN(cpu, m, (uint8_t)n, P.budget);
        if (r.out_e != poly_eval(task, n)) return 0;
    }
    return 1;
}

// Animation status of a program in its niche: 2=solves its task, 1=replicator,
// 0=other. Uses the fast core directly (reentrant). Solver check short-circuits.
static uint8_t anim_status(const uint8_t* prog, int task){
    uint8_t m[SB_MEM], e; int h;
    int sv=1;
    for(int n=0;n<16;n++){ memcpy(m,prog,32); memset(m+32,0,32);
        zf_run(m,(uint8_t)n,P.budget,&e,&h); if(e!=poly_eval(task,n)){ sv=0; break; } }
    if(sv) return 2;
    memcpy(m,prog,32); for(int i=0;i<32;i++) m[32+i]=(uint8_t)(0x5A^(i*13));
    zf_run(m,0,P.budget,&e,&h);
    int f=0,r=0; for(int i=0;i<32;i++){ if(m[32+i]==prog[i])f++; if(m[32+i]==prog[31-i])r++; }
    return ((f>r?f:r)>=28) ? 1 : 0;
}

// ------------------------------- main loop ----------------------------------
int main(int argc, char** argv){
    // defaults
    P.L=1; P.W=128; P.H=128; P.epochs=30000;
    P.p_base=0.3; P.p_succ=1.0; P.C=0.3; P.pi=0.05;
    P.mut_inv=64; P.budget=512; P.seed=1; P.log_every=100;
    P.single_task=10 /* 2n+1 */; P.plant=0; P.plant_solver=0; P.plant_dud=0;
    P.notasks=0; P.anim=0; P.animout=NULL; P.out_csv="results/run.csv"; P.dump=NULL;
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
        else if(!strcmp(argv[i],"--anim")) P.anim=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--animout")) P.animout=argv[++i];
        else if(!strcmp(argv[i],"--log")) P.log_every=atol(argv[++i]);
        else if(!strcmp(argv[i],"--out")) P.out_csv=argv[++i];
        else if(!strcmp(argv[i],"--budget")) P.budget=atoi(argv[++i]);
        else { fprintf(stderr,"unknown arg %s\n",argv[i]); return 1; }
    }
    NICHE_SZ = P.W*P.H;
    NPROG = P.L * NICHE_SZ;
    TAPES = malloc((size_t)NPROG*32);

    // initialize all programs with random bytes
    Rng gr = { sm64(P.seed ^ 0xABCDEF) };
    for(size_t i=0;i<(size_t)NPROG*32;i++) TAPES[i] = (uint8_t)(rnext(&gr)>>24);

    // optional: plant canonical LDIR replicators [1E 20 ED B0 ...] (diagnostic
    // for verifying takeover dynamics, not used in the emergence experiment)
    if (P.plant>0){
        uint8_t rep[32]; memset(rep,0,32);
        rep[0]=0x1E; rep[1]=0x20; rep[2]=0xED; rep[3]=0xB0;   // LD E,32 ; LDIR
        for(int k=0;k<P.plant;k++){ int idx=rint(&gr,NPROG); memcpy(tape(idx),rep,32); }
        fprintf(stderr,"# planted %d LDIR replicators\n", P.plant);
    }
    // Matched genotypes for the competence-gating demo: both use the same bounded
    // LDIR copy mechanism; they differ ONLY in whether they solve 2n+1.
    //  solver+replicator: LD A,D; ADD A,A; INC A; LD BC,32; LD DE,32; LDIR; LD E,A; HALT
    //  non-solving dud  : (NOP NOP NOP);        LD BC,32; LD DE,32; LDIR; (NOP);   HALT
    if (P.plant_solver>0){
        uint8_t g[32]; memset(g,0,32);
        uint8_t c[]={0x7A,0x87,0x3C,0x01,0x20,0x00,0x11,0x20,0x00,0xED,0xB0,0x5F,0x76};
        memcpy(g,c,sizeof c);
        for(int k=0;k<P.plant_solver;k++){ int idx=rint(&gr,NPROG); memcpy(tape(idx),g,32); }
        fprintf(stderr,"# planted %d solver+replicators (solve 2n+1 AND copy)\n", P.plant_solver);
    }
    if (P.plant_dud>0){
        uint8_t g[32]; memset(g,0,32);
        uint8_t c[]={0x00,0x00,0x00,0x01,0x20,0x00,0x11,0x20,0x00,0xED,0xB0,0x00,0x76};
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
    fprintf(csv,"epoch,selected_frac,repl_sig_frac,ldir_frac,loadpush_frac,"
                "func_repl_frac,true_repl_frac,solve_frac,niches_solved,avg_steps,halt_frac,uniq_frac,zero_frac\n");

    fprintf(stderr,"# soup: NPROG=%d (L=%d %dx%d) epochs=%ld C=%.2f pi=%.3f budget=%d threads=%d\n",
            NPROG, P.L, P.W, P.H, P.epochs, P.C, P.pi, P.budget,
#ifdef _OPENMP
            omp_get_max_threads()
#else
            1
#endif
    );

    // animation frame output: header [magic,W,H,L] then per-frame [epoch, NPROG*4 bytes]
    FILE* animf = NULL; uint8_t* framebuf = NULL;
    if (P.anim>0 && P.animout){
        animf = fopen(P.animout,"wb");
        int32_t hdr[4] = { 0x4D494E41 /*'ANIM'*/, P.W, P.H, P.L };
        fwrite(hdr,4,4,animf);
        framebuf = malloc((size_t)NPROG*4);
    }

    struct timespec t0; clock_gettime(CLOCK_MONOTONIC,&t0);

    for(long epoch=0; epoch<=P.epochs; epoch++){
        // ---- 1. mutation (parallel; per-index RNG => thread-count independent) ----
        #pragma omp parallel for schedule(static)
        for(int i=0;i<NPROG;i++){
            Rng mr = { sm64(P.seed*0x9E3779B97F4A7C15ULL + epoch*0x100000001B3ULL + (uint64_t)i*0xD1B54A32D192ED03ULL) };
            if (rint(&mr, P.mut_inv)==0){
                int pos = rint(&mr,32);
                tape(i)[pos] = (uint8_t)(rnext(&mr)>>24);
            }
        }

        // ---- 2. pairing (sequential, cheap): shuffle + greedy availability ----
        Rng sr = { sm64(P.seed*0x2545F491ULL + epoch*0x100000001B3ULL) };
        for(int i=NPROG-1;i>0;i--){ int j=rint(&sr,i+1); int tmp=order[i]; order[i]=order[j]; order[j]=tmp; }
        memset(avail,1,NPROG);
        int npair=0;
        for(int k=0;k<NPROG;k++){
            int p1 = order[k];
            if(!avail[p1]) continue;
            // Single random proposal, then filter (Methods 4.3): gives ~56%
            // selection because collisions with already-used programs are dropped.
            int p2;
            if (runif(&sr) < P.pi){
                p2 = rint(&sr, NPROG);                 // cross-niche pollination
            } else {
                p2 = neighbour(p1, rint(&sr,4));       // one random von Neumann neighbour
            }
            if(p2==p1 || !avail[p2]) continue;         // filtered duplicate/unavailable
            avail[p1]=avail[p2]=0;
            pair1[npair]=p1; pair2[npair]=p2; npair++;
        }

        // ---- 3+4. validation + interaction (parallel over disjoint pairs) ----
        long val_ct=0, inter_ct=0;
        #pragma omp parallel reduction(+:val_ct,inter_ct)
        {
            z80_t cpu;
            #pragma omp for schedule(dynamic,256)
            for(int q=0;q<npair;q++){
                int p1=pair1[q], p2=pair2[q];
                Rng pr = { sm64(P.seed*0xD1B54A32D192ED03ULL + epoch*0x9E3779B1ULL + (uint64_t)q*0x100000001B3ULL) };
                double p;
                if (P.notasks){
                    p = 1.0;                       // Fig 2D control: no validation, always interact
                } else {
                    double avg_k=0;
                    int validated = validate(&cpu, p1, &pr, &avg_k);
                    if(validated) val_ct++;
                    p = validated ? (P.p_succ - P.C*avg_k/P.budget) : P.p_base;
                    if(p<0) p=0;
                }
                if (runif(&pr) < p){
                    interact(&cpu, p1, p2);
                    inter_ct++;
                }
            }
        }

        // ---- logging ----
        if (epoch % P.log_every == 0 || epoch==P.epochs){
            const int SAMPLE = NPROG < 4096 ? NPROG : 4096;
            long sig=0, ldir=0, lp=0, frepl=0, trepl=0, solve=0, hlt=0, stp=0, zerob=0;
            #pragma omp parallel reduction(+:sig,ldir,lp,frepl,trepl,solve,hlt,stp,zerob)
            {
                z80_t cpu;
                #pragma omp for
                for(int s=0;s<SAMPLE;s++){
                    int idx = (NPROG<=SAMPLE) ? s : (int)( sm64(P.seed+epoch*777ULL+s) % NPROG );
                    const uint8_t* t = tape(idx);
                    int isl = is_ldir_family(t), ip = is_loadpush_family(t);
                    if(isl) ldir++;
                    if(ip) lp++;
                    if(isl||ip) sig++;
                    for(int b=0;b<32;b++) if(t[b]==0) zerob++;
                    int steps=0, halted=0;
                    int cp = self_copy_bytes(&cpu, t, &steps, &halted);
                    if(cp >= 30){ frepl++;
                        // recursive check: does the copied offspring itself replicate?
                        uint8_t m[SB_MEM]; memcpy(m,t,32);
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
            // niches solved (>=10% of a niche's programs solve all 16 inputs)
            int niches_solved=0;
            #pragma omp parallel for reduction(+:niches_solved)
            for(int nch=0;nch<P.L;nch++){
                z80_t cpu; int base=nch*NICHE_SZ; int task=(P.L==1)?P.single_task:nch;
                int chk = NICHE_SZ<1024?NICHE_SZ:1024; long good=0;
                for(int s=0;s<chk;s++){
                    int idx = base + (NICHE_SZ<=chk? s : (int)(sm64((uint64_t)nch*131ULL+epoch+s)%NICHE_SZ));
                    if(solves_all(&cpu, tape(idx), task)) good++;
                }
                if ((double)good/chk >= 0.10) niches_solved++;
            }
            // homogenization proxy: fraction of random tape pairs that differ
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
                (double)sig/SAMPLE,(double)ldir/SAMPLE,(double)lp/SAMPLE,
                (double)frepl/SAMPLE,(double)trepl/SAMPLE,(double)solve/SAMPLE, niches_solved,
                avg_steps, (double)hlt/SAMPLE, uniq, zero_frac);
            fflush(csv);
            fprintf(stderr,"epoch %7ld  sel=%.2f  repl_sig=%.3f frepl=%.3f trepl=%.3f  solve=%.4f n=%d/%d  avgk=%.0f zero=%.2f  [%.1fs]\n",
                epoch, sel, (double)sig/SAMPLE, (double)frepl/SAMPLE, (double)trepl/SAMPLE,
                (double)solve/SAMPLE, niches_solved, P.L, avg_steps, zero_frac, secs);
            fflush(stderr);
        }
        // ---- animation frame (denser during early emergence) ----
        if (animf){
            int due = (epoch<3000) ? (epoch%200==0) : (epoch%P.anim==0);
            if (due){
                #pragma omp parallel for schedule(dynamic,512)
                for(int i=0;i<NPROG;i++){
                    const uint8_t* t=tape(i);
                    framebuf[i*4+0]=t[0]; framebuf[i*4+1]=t[1]; framebuf[i*4+2]=t[2];
                    framebuf[i*4+3]=anim_status(t, task_of(i));
                }
                int32_t ep=(int32_t)epoch;
                fwrite(&ep,4,1,animf); fwrite(framebuf,1,(size_t)NPROG*4,animf); fflush(animf);
            }
        }

        (void)val_ct;(void)inter_ct;
    }
    if (animf){ fclose(animf); free(framebuf);
        fprintf(stderr,"# wrote animation frames to %s\n", P.animout); }
    fclose(csv);
    if (P.dump){
        FILE* df = fopen(P.dump,"wb");
        if(df){ fwrite(TAPES,1,(size_t)NPROG*32,df); fclose(df);
                fprintf(stderr,"# dumped %d tapes (%zu bytes) to %s\n",NPROG,(size_t)NPROG*32,P.dump); }
    }
    free(TAPES);free(order);free(avail);free(pair1);free(pair2);
    return 0;
}
