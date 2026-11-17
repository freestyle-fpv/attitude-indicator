#include "panel_sync.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
uint32_t fakeNow=0,pulseTime=100000;bool pulsePresent=true;
void (*fakeEdge)(void*)=nullptr;void* fakeArg=nullptr;
int main(){
  instrument::PanelSync sync;sync.begin(13);
  assert(sync.wait());
  // Measured central write/scan margins span -2966..2578 us. At TE alone,
  // different parts of one ladder land on opposite sides of the scan.
  const uint32_t phase=fakeNow-pulseTime;
  assert(phase>=4000&&phase<5000);
  for(int margin:{-2966,-1400,-52,230,320,611,1831,2269,2578}){
    assert(margin-int(phase)<0);assert(margin-int(phase)>-16480);
  }
  pulseTime+=16480;assert(sync.wait(0));assert(fakeNow==pulseTime);
  // A frame that becomes ready shortly after a new pulse can still hit the
  // same confirmed start phase. A used pulse can authorize only one frame.
  pulseTime+=16480;fakeNow=pulseTime;fakeEdge(fakeArg);fakeNow+=1600;
  assert(sync.wait(4120,true));assert(fakeNow==pulseTime+4120);
  pulseTime+=16480;assert(sync.wait(4120,true));assert(fakeNow==pulseTime+4120);
  pulseTime+=16480;fakeNow=pulseTime;fakeEdge(fakeArg);fakeNow+=8000;
  assert(sync.wait(4120,true,11000));assert(fakeNow==pulseTime+8000);
  // The edge is consumed only once. A second frame must use the next pulse.
  pulseTime+=16480;assert(sync.wait(4120,true,11000));assert(fakeNow==pulseTime+4120);
  pulseTime+=16480;fakeNow=pulseTime;fakeEdge(fakeArg);fakeNow+=11200;
  pulseTime+=16480;assert(sync.wait(4120,true,11000));assert(fakeNow==pulseTime+4120);
  // A pulse too close to the start deadline is deferred to the next refresh.
  pulseTime+=16480;fakeNow=pulseTime;fakeEdge(fakeArg);fakeNow+=4000;
  pulseTime+=16480;assert(sync.wait(4120,true));assert(fakeNow==pulseTime+4120);
  pulseTime=0xFFFFFF00u;assert(sync.wait(4120));assert(uint32_t(fakeNow-pulseTime)==4120);
  pulsePresent=false;const uint32_t before=fakeNow;assert(!sync.wait());
  assert(uint32_t(fakeNow-before)==30000&&sync.misses()==1);
  std::puts("PASS HUD transmission stays behind the current scan within the next scan window");
}
