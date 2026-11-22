#include "hardware_diagnostics.h"
#include <cassert>
#include <cstring>
#include <cstdio>
struct Bus{
 uint8_t reg[256]={};unsigned ticks=0;bool failRead=false,stuckHigh=false,stuckLow=false;
 Bus(){reg[3]=0x15;reg[4]=0x65;reg[8]=3;}
 bool read(uint8_t address,uint8_t* out,size_t n){if(failRead)return false;if(address==0x2d){*out=stuckHigh?1:stuckLow?0:(((reg[3]|reg[4])&0x80)&&reg[8]==0?1:0);return true;}std::memcpy(out,reg+address,n);return true;}
 bool write(uint8_t address,uint8_t value){reg[address]=value;return true;}
 void wait(){++ticks;}
 void response(int16_t value){for(int i=0;i<3;i++){reg[0x51+i*2]=value&255;reg[0x52+i*2]=(uint16_t(value)>>8)&255;}}
};
int main(){
 Bus a;a.response(-512);auto ar=instrument::imuSelfTest(a,false);assert(ar.code==1&&ar.restored&&ar.response.x==-256);assert(a.reg[3]==0x15&&a.reg[4]==0x65&&a.reg[8]==3);
 Bus g;g.response(-5600);auto gr=instrument::imuSelfTest(g,true);assert(gr.code==1&&gr.response.z==-350&&gr.restored);
 Bus threshold;threshold.response(4800);assert(instrument::imuSelfTest(threshold,true).code==0);
 Bus high;high.stuckHigh=true;auto hr=instrument::imuSelfTest(high,true);assert(hr.code==4&&hr.restored&&high.ticks<=400);
 Bus low;low.stuckLow=true;assert(instrument::imuSelfTest(low,false).code==4);assert(low.reg[8]==3);
 Bus broken;broken.failRead=true;assert(instrument::imuSelfTest(broken,false).code==3);assert(broken.reg[8]==3);
 uint32_t memory[258];memory[0]=0x12345678;memory[257]=0x87654321;assert(instrument::testMemoryBlock(memory+1,256));assert(memory[0]==0x12345678&&memory[257]==0x87654321);
 instrument::DiagnosticCadence cadence;unsigned emitted=0;
 for(uint32_t now=0;now<10000;now+=24)if(cadence.due(now))++emitted;
 assert(emitted>=198&&emitted<=201);
 instrument::DiagnosticLease lease;assert(!lease.active(0));lease.renew(0xfffffff0);assert(lease.active(10));assert(!lease.active(20000));lease.stop();assert(!lease.active(10));
 std::puts("PASS bounded IMU self-test handshake, signed responses, thresholds, configuration restore, memory guards and stream lease");
}
