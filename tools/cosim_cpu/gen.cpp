#include <vector>
#include <cstdlib>
#include "bus.h"
#include "cpu_6502.h"
#include "cpu_opcodes.h"
int main(int c,char**v){int seed=atoi(v[1]);srand(seed*7919);static Bus b;for(auto&x:b.m)x=rand();int p=0x200;auto e=[&](int x){b.m[p++]=x;};auto R=[&](){return (rand()>>3)&255;};
auto sta=[&](int a){e(0x8D);e(a);e(a>>8);};
b.m[0xFFFC]=0;b.m[0xFFFD]=2;b.m[0xFFFE]=0;b.m[0xFFFF]=0xEF;b.m[0xEF00]=0x40;
e(0xD8);FILE*o=fopen("ops.txt","w");
while(p<0xBF00){int op=R();Opcode k=kOpcodes[op];if(k.op==JAM)continue;int x=R(),y=R(),z=R();int hi=0xC0+(R()&15),lo=R();
 int P=R(), a=R();int sp=0x80+(R()&0x7f);
 if(k.op==RTS||k.op==RTI||k.op==TXS||k.op==SHS){if(k.op==RTS||k.op==RTI)sp=0xC0;}
 fprintf(o,"%02X\n",op);
 e(0xA2);e(sp);e(0x9A);
 auto poke=[&](int ad,int val){e(0xA9);e(val);sta(ad);};
 bool done=false;
 if(k.modo==IZX||k.modo==IZY){int q=k.modo==IZX?(z+x)&255:z;poke(q,lo);poke((q+1)&255,hi);}
 int back_pos=-1;
 if(k.op==RTS||k.op==RTI||k.modo==IND){ // destino calculado despues: se parchea
  if(k.modo==IND){int pl=(R()&1)?0xFF:R();int pa=0xC000|((R()&7)<<8)|pl;e(0xA9);int l=p;e(0);sta(pa);e(0xA9);int h=p;e(0);sta((pa&0xFF00)|((pa+1)&255));
   e(0xA2);e(x);e(0xA0);e(y);e(0xA9);e(a);e(op);e(pa);e(pa>>8);b.m[l]=p;b.m[h]=p>>8;}
  else{e(0xA9);int h=p;e(0);e(0x48);e(0xA9);int l=p;e(0);e(0x48);if(k.op==RTI){e(0xA9);e(P);e(0x48);}e(0xA2);e(x);e(0xA0);e(y);e(op);int t=k.op==RTS?p-1:p;b.m[l]=t;b.m[h]=t>>8;}
  done=true;}
 if(!done){e(0xA9);e(P);e(0x48);e(0xA9);e(a);e(0xA2);e(x);e(0xA0);e(y);e(0x28);
 e(op);
 switch(k.modo){case IMP:case ACC:if(k.op==BRK)e(0);break;case IMM:e(R());break;case REL:{int n=R()&63;e(n);for(int i=0;i<n;i++)e((i&1)?0xC8:0xEA);break;}
 case ZP:case ZPX:case ZPY:case IZX:case IZY:e(z);break;
 case ABS:if(k.op==JMP||k.op==JSR){int t=p+2;e(t);e(t>>8);}else{e(lo);e(hi);}break;
 default:e(lo);e(hi);}}
 e(0x08);sta(0xF000);e(0x8E);e(1);e(0xF0);e(0x8C);e(2);e(0xF0);e(0x68);sta(0xF003);e(0xBA);e(0x8E);e(4);e(0xF0);e(0xD8);}
sta(0xF0FF);fclose(o);
FILE*h=fopen("mem.hex","w");for(int i=0;i<65536;i++)fprintf(h,"%d\n",b.m[i]);fclose(h);
b.f=fopen("emu.txt","w");b.ts.g=fopen("emu.cyc","w");Cpu6502 cpu(b);cpu.reset();long n=0;while(!b.fin&&n++<400000){cpu.step();b.cyc+=cpu.last_cycles(); }
fclose(b.f);fclose(b.ts.g);}
