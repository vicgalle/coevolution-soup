// analyze6502.c — verify a dumped 6502-soup population for genuine replication.
//
// The 6502 analog of analyze.c. Loads NPROG*32 raw bytes, reports functional
// self-copy / recursive-replicator / task-solve rates, the most common tapes,
// and disassembles example evolved replicators so their copy architecture
// (indexed-store loop vs read-backward stack push) can be read off.
#define CHIPS_IMPL
#define CHIPS_ASSERT(c) ((void)0)
#include <assert.h>
#include "m6502.h"
#include "sandbox6502.h"
#include "disasm6502.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int c0,c1,c2,c3; const char* name; } Poly;
static const Poly TASKS[32] = {
    {0,1,0,0,"n"},{1,1,0,0,"n+1"},{2,1,0,0,"n+2"},{3,1,0,0,"n+3"},{-1,1,0,0,"n-1"},
    {-2,1,0,0,"n-2"},{0,-1,0,0,"-n"},{0,2,0,0,"2n"},{0,3,0,0,"3n"},{0,4,0,0,"4n"},
    {1,2,0,0,"2n+1"},{3,-2,0,0,"-2n+3"},{1,3,0,0,"3n+1"},{7,3,0,0,"3n+7"},{2,5,0,0,"5n+2"},
    {3,5,0,0,"5n+3"},{-2,5,0,0,"5n-2"},{7,11,0,0,"11n+7"},{0,0,1,0,"n^2"},{2,0,-1,0,"-n^2+2"},
    {0,2,1,0,"n^2+2n"},{1,2,1,0,"n^2+2n+1"},{1,3,1,0,"n^2+3n+1"},{0,-1,1,0,"n^2-n"},
    {0,1,2,0,"2n^2+n"},{1,2,-1,0,"-n^2+2n+1"},{1,1,1,0,"n^2+n+1"},{0,0,0,1,"n^3"},
    {0,1,1,1,"n^3+n^2+n"},{3,1,1,1,"n^3+n^2+n+3"},{0,1,0,1,"n^3+n"},{1,0,0,1,"n^3+1"},
};
static inline uint8_t poly_eval(int t,int n){ const Poly*p=&TASKS[t];
    long v=p->c0+p->c1*(long)n+p->c2*(long)n*n+p->c3*(long)n*n*n; return (uint8_t)(((v%256)+256)%256);}

static int contains1(const uint8_t*t,uint8_t b){ for(int i=0;i<32;i++) if(t[i]==b) return 1; return 0; }
static int has_storeloop(const uint8_t*t){
    return contains1(t,0x95)||contains1(t,0x9D)||contains1(t,0x99)||contains1(t,0x81)||contains1(t,0x91); }
static int has_stackcopy(const uint8_t*t){ return contains1(t,0x48); }

static void fill_partner(uint8_t*p){ for(int i=0;i<32;i++) p[i]=(uint8_t)(0x5A ^ (i*13)); }

static int run_copy(m6502_t*cpu,const uint8_t*prog,const uint8_t*partner,uint8_t*offspring,int* fwd_out){
    uint8_t m[SB6_MEM]; memcpy(m,prog,32); memcpy(m+32,partner,32);
    sb6_run(cpu,m,0,SB6_BUDGET);
    memcpy(offspring,m+32,32);
    int fwd=0,rev=0; for(int i=0;i<32;i++){if(m[32+i]==prog[i])fwd++; if(m[32+i]==prog[31-i])rev++;}
    if(fwd_out)*fwd_out=(fwd>=rev);
    return fwd>rev?fwd:rev;
}
static int true_replicator(m6502_t*cpu,const uint8_t*prog){
    uint8_t partner[32]; fill_partner(partner);
    uint8_t child[32]; if(run_copy(cpu,prog,partner,child,0) < 28) return 0;
    uint8_t grand[32]; return run_copy(cpu,child,partner,grand,0) >= 28;
}
static int solves(m6502_t*cpu,const uint8_t*prog,int task){
    uint8_t m[SB6_MEM];
    for(int n=0;n<16;n++){memcpy(m,prog,32);memset(m+32,0,32);
        sb6_result r=sb6_run(cpu,m,(uint8_t)n,SB6_BUDGET); if(r.out_a!=poly_eval(task,n))return 0;}
    return 1;
}

static uint8_t* TAPES; static int NPROG;
static int cmp32(const void*a,const void*b){return memcmp(a,b,32);}

int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"usage: %s dumpfile [task_index]\n",argv[0]);return 1;}
    int task = argc>2?atoi(argv[2]):10;
    FILE*f=fopen(argv[1],"rb"); if(!f){perror("open");return 1;}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    NPROG=sz/32; TAPES=malloc(sz); if(fread(TAPES,1,sz,f)!=(size_t)sz){return 1;} fclose(f);
    printf("Loaded %d tapes from %s (task=%s)\n",NPROG,argv[1],TASKS[task].name);

    m6502_t cpu;
    long sl=0,sc=0,frepl=0,trepl=0,solve=0,fwdrep=0,revrep=0;
    for(int i=0;i<NPROG;i++){
        const uint8_t*t=TAPES+(size_t)i*32;
        if(has_storeloop(t))sl++;
        if(has_stackcopy(t))sc++;
        uint8_t off[32],partner[32]; fill_partner(partner); int isfwd=0;
        if(run_copy(&cpu,t,partner,off,&isfwd)>=30){frepl++; if(isfwd)fwdrep++; else revrep++;}
        if(true_replicator(&cpu,t))trepl++;
        if(solves(&cpu,t,task))solve++;
    }
    printf("\n== population functional analysis (n=%d) ==\n",NPROG);
    printf("  indexed-store byte present : %6.2f%%\n",100.0*sl/NPROG);
    printf("  PHA (stack) byte present   : %6.2f%%\n",100.0*sc/NPROG);
    printf("  functional self-copy       : %6.2f%%  (>=30/32 bytes, fwd or rev, vs blank)\n",100.0*frepl/NPROG);
    printf("      of which forward       : %6.2f%%   reversed : %6.2f%%\n",100.0*fwdrep/NPROG,100.0*revrep/NPROG);
    printf("  TRUE replicator            : %6.2f%%  (offspring itself reproduces)\n",100.0*trepl/NPROG);
    printf("  solves %-12s     : %6.2f%%  (correct on all 16 inputs)\n",TASKS[task].name,100.0*solve/NPROG);

    uint8_t* sorted=malloc(sz); memcpy(sorted,TAPES,sz);
    qsort(sorted,NPROG,32,cmp32);
    printf("\n== top 12 most common tapes ==\n");
    int shown=0;
    for(int i=0;i<NPROG && shown<12;){
        int j=i; while(j<NPROG && memcmp(sorted+(size_t)j*32,sorted+(size_t)i*32,32)==0) j++;
        int count=j-i;
        if(count>=2){
            const uint8_t*t=sorted+(size_t)i*32;
            uint8_t off[32],partner[32]; fill_partner(partner); int isfwd=0;
            int cp=run_copy(&cpu,t,partner,off,&isfwd);
            int tr=true_replicator(&cpu,t);
            printf(" [%5d x %5.1f%%] ",count,100.0*count/NPROG);
            for(int b=0;b<32;b++)printf("%02X",t[b]);
            printf("  copy=%2d/32%s %s%s\n",cp,isfwd?"F":"R",
                tr?"TRUE-REPL ":"", solves(&cpu,t,task)?"SOLVES":"");
            shown++;
        }
        i=j;
    }
    long distinct=0; for(int i=0;i<NPROG;){int j=i;while(j<NPROG&&memcmp(sorted+(size_t)j*32,sorted+(size_t)i*32,32)==0)j++;distinct++;i=j;}
    printf("\n  distinct tapes: %ld / %d  (%.2f%% unique)\n",distinct,NPROG,100.0*distinct/NPROG);

    printf("\n== example evolved replicators (disassembled) ==\n");
    int ex=0;
    for(int i=0;i<NPROG && ex<6;i++){
        const uint8_t*t=TAPES+(size_t)i*32;
        if(!true_replicator(&cpu,t)) continue;
        char dis[256]; disasm6502_tape(t,16,dis,sizeof dis);
        printf(" #%d ",ex+1); for(int b=0;b<32;b++)printf("%02X",t[b]);
        printf("\n     %s%s\n", dis, solves(&cpu,t,task)?"  [SOLVES]":"");
        ex++; i += NPROG/40;
    }
    free(TAPES);free(sorted);
    return 0;
}
