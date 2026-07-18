// grid_analyze.c — per-niche analysis of a 32-niche soup dump.
// Each niche i carries task i; reports per-niche solve% and replicator%, and
// disassembles an example evolved solver+replicator.
#define CHIPS_IMPL
#include "z80.h"
#include "sandbox.h"
#include "disasm.h"
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
static inline uint8_t pe(int t,int n){const Poly*p=&TASKS[t];
 long v=p->c0+p->c1*(long)n+p->c2*(long)n*n+p->c3*(long)n*n*n;return (uint8_t)(((v%256)+256)%256);}

static int solves(z80_t*c,const uint8_t*p,int t){uint8_t m[SB_MEM];
 for(int n=0;n<16;n++){memcpy(m,p,32);memset(m+32,0,32);sb_result r=sb_run(c,m,(uint8_t)n,SB_BUDGET);
   if(r.out_e!=pe(t,n))return 0;}return 1;}
static int replicates(z80_t*c,const uint8_t*p){uint8_t m[SB_MEM];memcpy(m,p,32);
 for(int i=0;i<32;i++)m[32+i]=(uint8_t)(0x5A^(i*13));sb_run(c,m,0,SB_BUDGET);
 int f=0,r=0;for(int i=0;i<32;i++){if(m[32+i]==p[i])f++;if(m[32+i]==p[31-i])r++;}return (f>r?f:r)>=28;}

int main(int argc,char**argv){
 const char*path=argc>1?argv[1]:"results/grid32_1e5.bin";
 int W=128,H=128,NSZ=W*H;
 FILE*f=fopen(path,"rb");if(!f){perror("open");return 1;}
 fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
 int NPROG=sz/32,L=NPROG/NSZ;
 uint8_t*T=malloc(sz);if(fread(T,1,sz,f)!=(size_t)sz)return 1;fclose(f);
 printf("Loaded %d programs, %d niches of %dx%d\n\n",NPROG,L,W,H);
 printf("niche  task          solve%%  repl%%   solve+repl%%  status\n");
 printf("-----  ------------  ------  ------  -----------  ------\n");
 z80_t cpu; int nsolved=0;
 int solved_list[64],nsl=0, unsolved[64],nun=0;
 for(int nch=0;nch<L;nch++){
   int base=nch*NSZ; int S=2000; long sol=0,rep=0,both=0;
   for(int s=0;s<S;s++){ int idx=base+(int)((uint64_t)s*2654435761u%NSZ);
     const uint8_t*p=T+(size_t)idx*32; int so=solves(&cpu,p,nch),re=replicates(&cpu,p);
     if(so)sol++; if(re)rep++; if(so&&re)both++; }
   double sp=100.0*sol/S, rp=100.0*rep/S, bp=100.0*both/S;
   int ok = sp>=10.0; if(ok){nsolved++; solved_list[nsl++]=nch;} else unsolved[nun++]=nch;
   printf("%3d    %-12s  %5.1f   %5.1f   %6.1f       %s\n",
          nch,TASKS[nch].name,sp,rp,bp, ok?"SOLVED":"-");
 }
 printf("\n==> %d/%d niches solved (>=10%% of programs solve all 16 inputs)\n",nsolved,L);
 printf("Unsolved niches: "); for(int i=0;i<nun;i++)printf("%s ",TASKS[unsolved[i]].name); printf("\n");

 // disassemble an example solver+replicator from the first solved niche
 for(int k=0;k<nsl;k++){ int nch=solved_list[k]; int base=nch*NSZ;
   for(int s=0;s<NSZ;s++){ const uint8_t*p=T+(size_t)(base+s)*32;
     if(solves(&cpu,p,nch)&&replicates(&cpu,p)){
       char dis[300]; disasm_tape(p,24,dis,sizeof dis);
       printf("\nExample evolved SOLVER+REPLICATOR (niche %d, task %s):\n  ",nch,TASKS[nch].name);
       for(int b=0;b<32;b++)printf("%02X",p[b]);
       printf("\n  %s\n",dis);
       goto done; } } }
 done: free(T); return 0;
}
