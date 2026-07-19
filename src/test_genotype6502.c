// test_genotype6502.c — verify the 6502 sandbox with hand-written programs.
//
// Checks that sb6_run emulates correctly by exercising:
//   (1) an indexed-store copy loop  (LDA zp,X / STA zp,X / DEX / BPL) -> exact
//       32-byte forward self-copy, the 6502 analog of the Z80's compact LDIR
//       replicator (a copy LOOP, since the 6502 has no block-move instruction);
//   (2) a read-backward stack-push copy (LDA zp,X / PHA / DEX / BPL) -> forward
//       self-copy via the descending stack, the 6502 analog of Load-Push;
//   (3) a naive forward stack-push copy -> REVERSED (non-functional) copy, to
//       show why the stack route must read the source backwards to replicate;
//   (4) solvers for f(x) = n, 2n+1, n^2 -> correct output on all 16 inputs.
#define CHIPS_IMPL
#define CHIPS_ASSERT(c) ((void)0)
#include <assert.h>
#include "m6502.h"
#include "sandbox6502.h"
#include <stdio.h>
#include <string.h>

static void show(const char* name, const uint8_t* prog){
    printf("%-28s ", name);
    for(int i=0;i<32;i++) printf("%02X", prog[i]);
    printf("\n");
}

// Run prog against a non-zero partner (interaction), report best copy alignment.
static void test_copy(const char* name, const uint8_t* code, int clen){
    m6502_t cpu;
    uint8_t prog[32]; memset(prog,0,32); memcpy(prog,code,clen);
    // distinctive bytes in the free region, to confirm they are copied too
    for(int i=clen;i<32;i++) prog[i]=(uint8_t)(0xC0+i);
    show(name, prog);

    uint8_t m[SB6_MEM]; memcpy(m,prog,32);
    for(int i=0;i<32;i++) m[32+i]=(uint8_t)(0x5A ^ (i*13));   // non-zero partner
    sb6_result r = sb6_run(&cpu, m, 0, SB6_BUDGET);
    int fwd=0, rev=0;
    for(int i=0;i<32;i++){ if(m[32+i]==prog[i])fwd++; if(m[32+i]==prog[31-i])rev++; }
    printf("    copy: forward=%2d/32  reversed=%2d/32   steps=%d halted=%d  -> %s\n\n",
           fwd, rev, r.steps, r.halted,
           fwd>=30?"FORWARD REPLICATOR":(rev>=30?"reversed copy (non-functional)":"no copy"));
}

// value = (c0 + c1 n + c2 n^2) mod 256
static uint8_t pe(int c0,int c1,int c2,int n){ long v=c0+c1*(long)n+c2*(long)n*n; return (uint8_t)(((v%256)+256)%256); }

static void test_solver(const char* name,const uint8_t* code,int clen,int c0,int c1,int c2){
    m6502_t cpu;
    uint8_t prog[32]; memset(prog,0,32); memcpy(prog,code,clen);
    show(name, prog);
    int ok=1, maxk=0;
    for(int n=0;n<16;n++){
        uint8_t m[SB6_MEM]; memcpy(m,prog,32); memset(m+32,0,32);
        sb6_result r=sb6_run(&cpu,m,(uint8_t)n,SB6_BUDGET);
        uint8_t want=pe(c0,c1,c2,n);
        if(r.out_a!=want){ ok=0; printf("    n=%2d: A=%3d want=%3d FAIL (steps=%d)\n",n,r.out_a,want,r.steps); }
        if(r.steps>maxk) maxk=r.steps;
    }
    printf("    %s  (max k=%d steps)\n\n", ok?"SOLVES all 16 inputs":"FAILS", maxk);
}

int main(void){
    printf("== 6502 sandbox verification ==\n\n");

    // (1) indexed-store copy loop: A2 1F  B5 00  95 20  CA  10 F9  02
    //   LDX #$1F ; loop: LDA $00,X ; STA $20,X ; DEX ; BPL loop ; JAM
    uint8_t idx[] = {0xA2,0x1F, 0xB5,0x00, 0x95,0x20, 0xCA, 0x10,0xF9, 0x02};
    test_copy("indexed-store copy loop", idx, sizeof idx);

    // (2) read-backward stack-push copy: A2 1F  B5 00  48  CA  10 FA  02
    //   LDX #$1F ; loop: LDA $00,X ; PHA ; DEX ; BPL loop ; JAM
    //   reads mem[31..0], pushes to [63..32] -> forward copy via descending stack
    uint8_t psh[] = {0xA2,0x1F, 0xB5,0x00, 0x48, 0xCA, 0x10,0xFA, 0x02};
    test_copy("read-backward PHA copy", psh, sizeof psh);

    // (3) naive FORWARD stack-push copy: A2 00  B5 00  48  E8  E0 20  D0 F8  02
    //   LDX #$00 ; loop: LDA $00,X ; PHA ; INX ; CPX #$20 ; BNE loop ; JAM
    //   reads mem[0..31], pushes to [63..32] -> REVERSED copy (not a replicator)
    uint8_t fwd[] = {0xA2,0x00, 0xB5,0x00, 0x48, 0xE8, 0xE0,0x20, 0xD0,0xF8, 0x02};
    test_copy("forward PHA copy (reversed)", fwd, sizeof fwd);

    // (4) solvers.  input in X, output in A, JAM (0x02) to halt.
    //   f(x)=n:      TXA ; JAM                     -> 8A 02
    uint8_t s_n[]   = {0x8A, 0x02};
    test_solver("solver f(x)=n",   s_n,  sizeof s_n,  0,1,0);
    //   f(x)=2n+1:   TXA ; ASL A ; ORA #$01 ; JAM  -> 8A 0A 09 01 02
    uint8_t s_2n1[] = {0x8A, 0x0A, 0x09,0x01, 0x02};
    test_solver("solver f(x)=2n+1", s_2n1, sizeof s_2n1, 1,2,0);
    //   f(x)=n^2 (add x, x times; skip when x==0 to stay in budget):
    //     STX $10 ; TXA ; BEQ done ; TAY ; LDA #$00 ; CLC ;
    //     loop: ADC $10 ; DEY ; BNE loop ; done: JAM
    //   86 10  8A  F0 07  A8  A9 00  18  65 10  88  D0 FB  02
    uint8_t s_n2[]  = {0x86,0x10, 0x8A, 0xF0,0x07, 0xA8, 0xA9,0x00, 0x18,
                       0x65,0x10, 0x88, 0xD0,0xFB, 0x02};
    test_solver("solver f(x)=n^2",  s_n2, sizeof s_n2, 0,0,1);
    return 0;
}
