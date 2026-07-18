// disasm.h — compact Z80 disassembler, enough to read evolved replicators.
// Not exhaustive; unknown opcodes render as "DB nn". Returns instruction length.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char* _r8[8]  = {"B","C","D","E","H","L","(HL)","A"};
static const char* _rp[4]  = {"BC","DE","HL","SP"};
static const char* _rp2[4] = {"BC","DE","HL","AF"};
static const char* _cc[8]  = {"NZ","Z","NC","C","PO","PE","P","M"};
static const char* _alu[8] = {"ADD A,","ADC A,","SUB ","SBC A,","AND ","XOR ","OR ","CP "};

// Disassemble one instruction at mem[pc] (pc already masked by caller domain).
// buf must hold >= 32 chars. Returns byte length (1..4).
static int disasm1(const uint8_t* mem, int pc, char* buf){
    uint8_t op = mem[pc & 63];
    uint8_t n  = mem[(pc+1) & 63];
    uint8_t n2 = mem[(pc+2) & 63];
    uint16_t nn = (uint16_t)(n | (n2<<8));
    int x=op>>6, y=(op>>3)&7, z=op&7, p=(op>>4)&3, q=(op>>3)&1;

    if(op==0x00){strcpy(buf,"NOP");return 1;}
    if(op==0x76){strcpy(buf,"HALT");return 1;}
    if(op==0xED){ // block ops + ED-page (subset)
        uint8_t e=mem[(pc+1)&63];
        switch(e){
            case 0xB0:strcpy(buf,"LDIR");return 2; case 0xB8:strcpy(buf,"LDDR");return 2;
            case 0xA0:strcpy(buf,"LDI");return 2;  case 0xA8:strcpy(buf,"LDD");return 2;
            case 0xB1:strcpy(buf,"CPIR");return 2; case 0xB9:strcpy(buf,"CPDR");return 2;
            case 0x44:strcpy(buf,"NEG");return 2;  case 0x4D:strcpy(buf,"RETI");return 2;
            case 0x67:strcpy(buf,"RRD");return 2;  case 0x6F:strcpy(buf,"RLD");return 2;
            case 0x42:strcpy(buf,"SBC HL,BC");return 2; case 0x52:strcpy(buf,"SBC HL,DE");return 2;
            case 0x4A:strcpy(buf,"ADC HL,BC");return 2; case 0x5A:strcpy(buf,"ADC HL,DE");return 2;
            default: snprintf(buf,32,"ED %02X",e); return 2;
        }
    }
    if(op==0xCB){ uint8_t c=mem[(pc+1)&63]; int cy=(c>>3)&7,cz=c&7;
        const char* rot[8]={"RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"};
        if((c>>6)==0) snprintf(buf,32,"%s %s",rot[cy],_r8[cz]);
        else if((c>>6)==1) snprintf(buf,32,"BIT %d,%s",cy,_r8[cz]);
        else if((c>>6)==2) snprintf(buf,32,"RES %d,%s",cy,_r8[cz]);
        else snprintf(buf,32,"SET %d,%s",cy,_r8[cz]);
        return 2;
    }
    if(op==0xDD){strcpy(buf,"[IX]");return 1;} if(op==0xFD){strcpy(buf,"[IY]");return 1;}
    if(x==0){
        switch(z){
            case 0:
                if(y==0){strcpy(buf,"NOP");return 1;}
                if(y==1){strcpy(buf,"EX AF,AF'");return 1;}
                if(y==2){snprintf(buf,32,"DJNZ %+d",(int8_t)n+2);return 2;}
                if(y==3){snprintf(buf,32,"JR %+d",(int8_t)n+2);return 2;}
                snprintf(buf,32,"JR %s,%+d",_cc[y-4],(int8_t)n+2);return 2;
            case 1:
                if(q==0){snprintf(buf,32,"LD %s,$%04X",_rp[p],nn);return 3;}
                snprintf(buf,32,"ADD HL,%s",_rp[p]);return 1;
            case 2:
                if(q==0){ if(p==0){strcpy(buf,"LD (BC),A");return 1;} if(p==1){strcpy(buf,"LD (DE),A");return 1;}
                          if(p==2){snprintf(buf,32,"LD ($%04X),HL",nn);return 3;} snprintf(buf,32,"LD ($%04X),A",nn);return 3;}
                if(p==0){strcpy(buf,"LD A,(BC)");return 1;} if(p==1){strcpy(buf,"LD A,(DE)");return 1;}
                if(p==2){snprintf(buf,32,"LD HL,($%04X)",nn);return 3;} snprintf(buf,32,"LD A,($%04X)",nn);return 3;
            case 3: snprintf(buf,32,"%s %s",q?"DEC":"INC",_rp[p]);return 1;
            case 4: snprintf(buf,32,"INC %s",_r8[y]);return 1;
            case 5: snprintf(buf,32,"DEC %s",_r8[y]);return 1;
            case 6: snprintf(buf,32,"LD %s,$%02X",_r8[y],n);return 2;
            default:{const char* a[8]={"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"};strcpy(buf,a[y]);return 1;}
        }
    }
    if(x==1){ snprintf(buf,32,"LD %s,%s",_r8[y],_r8[z]); return 1; }
    if(x==2){ snprintf(buf,32,"%s%s",_alu[y],_r8[z]); return 1; }
    // x==3
    switch(z){
        case 0: snprintf(buf,32,"RET %s",_cc[y]); return 1;
        case 1: if(q==0){snprintf(buf,32,"POP %s",_rp2[p]);return 1;}
                {const char* s[4]={"RET","EXX","JP (HL)","LD SP,HL"};strcpy(buf,s[p]);return 1;}
        case 2: snprintf(buf,32,"JP %s,$%04X",_cc[y],nn); return 3;
        case 3: if(y==0){snprintf(buf,32,"JP $%04X",nn);return 3;}
                if(y==2){snprintf(buf,32,"OUT ($%02X),A",n);return 2;}
                if(y==3){snprintf(buf,32,"IN A,($%02X)",n);return 2;}
                if(y==4){strcpy(buf,"EX (SP),HL");return 1;} if(y==5){strcpy(buf,"EX DE,HL");return 1;}
                if(y==6){strcpy(buf,"DI");return 1;} strcpy(buf,"EI");return 1;
        case 4: snprintf(buf,32,"CALL %s,$%04X",_cc[y],nn); return 3;
        case 5: if(q==0){snprintf(buf,32,"PUSH %s",_rp2[p]);return 1;}
                if(p==0){snprintf(buf,32,"CALL $%04X",nn);return 3;} snprintf(buf,32,"DB %02X",op);return 1;
        case 6: snprintf(buf,32,"%s$%02X",_alu[y],n); return 2;
        default: snprintf(buf,32,"RST $%02X",y*8); return 1;
    }
}

// Disassemble the first `bytes` bytes of a 32-byte tape into a single line.
static void disasm_tape(const uint8_t* tape, int bytes, char* out, int outsz){
    int pc=0; out[0]=0; char b[40];
    while(pc<bytes){ int l=disasm1(tape,pc,b);
        int len=strlen(out); snprintf(out+len,outsz-len,"%s%s", len?" ; ":"", b);
        pc+=l; if(pc>=32) break; }
}
