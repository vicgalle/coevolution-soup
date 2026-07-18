// render_dump.c — convert a full-tape population dump into ONE compact animation
// frame ([b0,b1,b2,status] per program), for validating the animation aesthetic
// on a static snapshot before committing to a long run.
#include "z80fast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
static uint8_t status(const uint8_t*prog,int task){uint8_t m[64],e;int h;int sv=1;
 for(int n=0;n<16;n++){memcpy(m,prog,32);memset(m+32,0,32);zf_run(m,(uint8_t)n,512,&e,&h);if(e!=pe(task,n)){sv=0;break;}}
 if(sv)return 2; memcpy(m,prog,32);for(int i=0;i<32;i++)m[32+i]=(uint8_t)(0x5A^(i*13));zf_run(m,0,512,&e,&h);
 int f=0,r=0;for(int i=0;i<32;i++){if(m[32+i]==prog[i])f++;if(m[32+i]==prog[31-i])r++;}return (f>r?f:r)>=28?1:0;}

int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"usage: %s dump.bin out.anim [W H]\n",argv[0]);return 1;}
 int W=argc>3?atoi(argv[3]):128, H=argc>4?atoi(argv[4]):128, NSZ=W*H;
 FILE*f=fopen(argv[1],"rb");if(!f){perror("open");return 1;}
 fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
 int NPROG=sz/32,L=NPROG/NSZ; uint8_t*T=malloc(sz);
 if(fread(T,1,sz,f)!=(size_t)sz)return 1; fclose(f);
 uint8_t*fb=malloc((size_t)NPROG*4);
 #pragma omp parallel for schedule(dynamic,512)
 for(int i=0;i<NPROG;i++){const uint8_t*t=T+(size_t)i*32; int task=i/NSZ;
   fb[i*4]=t[0];fb[i*4+1]=t[1];fb[i*4+2]=t[2];fb[i*4+3]=status(t,task);}
 FILE*o=fopen(argv[2],"wb"); int32_t hdr[4]={0x4D494E41,W,H,L}; fwrite(hdr,4,4,o);
 int32_t ep=0; fwrite(&ep,4,1,o); fwrite(fb,1,(size_t)NPROG*4,o); fclose(o);
 printf("wrote 1 frame (%d niches %dx%d) to %s\n",L,W,H,argv[2]);
 return 0;
}
