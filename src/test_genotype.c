// test_genotype.c — verify a hand-designed "solver+replicator" 32-byte program
// that BOTH computes 2n+1 into E (validation) AND copies itself over a partner
// (interaction). This is the compact replicator-with-task-code architecture the
// paper describes (LDIR leaves the tape free for task code).
#define CHIPS_IMPL
#include "z80.h"
#include "sandbox.h"
#include <stdio.h>
#include <string.h>

int main(void){
    z80_t cpu;
    // A = 2D+1 (survives LDIR); set BC=32, DE=32, HL=0; LDIR copies P1->P2;
    // then E = A; HALT.
    //  7A       LD A,D
    //  87       ADD A,A
    //  3C       INC A          ; A = 2D+1
    //  01 20 00 LD BC,0x0020   ; count 32
    //  11 20 00 LD DE,0x0020   ; dest = second half
    //  ED B0    LDIR           ; copy HL(0..)->DE(32..), 32 bytes
    //  5F       LD E,A         ; E = 2D+1
    //  76       HALT
    uint8_t prog[32];
    memset(prog,0,32);
    uint8_t code[] = {0x7A,0x87,0x3C, 0x01,0x20,0x00, 0x11,0x20,0x00, 0xED,0xB0, 0x5F, 0x76};
    memcpy(prog, code, sizeof code);
    // put some distinctive bytes in the "free" region to check they're copied
    for(int i=(int)sizeof(code); i<32; i++) prog[i] = (uint8_t)(0xC0 + i);

    printf("genotype: ");
    for(int i=0;i<32;i++) printf("%02X", prog[i]); printf("\n\n");

    // ---- validation: E must equal (2n+1) mod 256 for n=0..15 ----
    int solve_ok=1, maxk=0;
    for(int n=0;n<16;n++){
        uint8_t m[SB_MEM]; memcpy(m,prog,32); memset(m+32,0,32);
        sb_result r = sb_run(&cpu, m, (uint8_t)n, SB_BUDGET);
        uint8_t want=(uint8_t)((2*n+1)&0xFF);
        if(r.out_e!=want){ solve_ok=0; printf("  n=%2d: E=%3d want=%3d  FAIL\n",n,r.out_e,want);}
        if(r.steps>maxk) maxk=r.steps;
    }
    printf("VALIDATION (2n+1): %s   max k=%d steps  (metabolic penalty C*k/512 = %.3f at C=0.3)\n",
           solve_ok?"PASS all 16 inputs":"FAIL", maxk, 0.3*maxk/512.0);

    // ---- interaction: copies itself over a NON-ZERO partner ----
    uint8_t m[SB_MEM]; memcpy(m,prog,32);
    for(int i=0;i<32;i++) m[32+i]=(uint8_t)(0x5A ^ (i*13));
    sb_result r = sb_run(&cpu, m, 0, SB_BUDGET);
    int fwd=0; for(int i=0;i<32;i++) if(m[32+i]==prog[i]) fwd++;
    printf("INTERACTION: partner-half matches program in %d/32 bytes  (E=%d, steps=%d) -> %s\n",
           fwd, r.out_e, r.steps, fwd>=30?"REPLICATES":"does not replicate");

    // ---- recursive: does the offspring itself both solve and replicate? ----
    uint8_t child[32]; memcpy(child, m+32, 32);
    int child_solves=1;
    for(int n=0;n<16;n++){
        uint8_t mm[SB_MEM]; memcpy(mm,child,32); memset(mm+32,0,32);
        sb_result rr=sb_run(&cpu,mm,(uint8_t)n,SB_BUDGET);
        if(rr.out_e!=(uint8_t)((2*n+1)&0xFF)) child_solves=0;
    }
    printf("OFFSPRING also solves 2n+1: %s\n", child_solves?"YES (heritable competence)":"no");
    return 0;
}
