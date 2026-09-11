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
  pulseTime=0xFFFFFF00u;assert(sync.wait(4120));assert(uint32_t(fakeNow-pulseTime)==4120);
  pulsePresent=false;const uint32_t before=fakeNow;assert(!sync.wait());
  assert(uint32_t(fakeNow-before)==30000&&sync.misses()==1);
  std::puts("PASS HUD transmission stays behind the current scan within the next scan window");
}
