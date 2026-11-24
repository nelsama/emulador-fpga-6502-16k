#pragma once
#include <cstdint>
#include <cstdio>
struct Bus{uint8_t m[65536];FILE*f=nullptr;long cyc=0;bool fin=false;
uint8_t read(uint16_t a){return m[a];}
void write(uint16_t a,uint8_t v){m[a]=v;if((a>>8)!=0xF0)fprintf(f,"%04X %02X\n",a,v);else{fprintf(f,"%04X %02X T\n",a,v);ts.push(cyc);if(a==0xF0FF)fin=true;}}
bool irq_asserted()const{return false;}
struct{FILE*g=nullptr;void push(long c){fprintf(g,"%ld\n",c);}}ts;};
