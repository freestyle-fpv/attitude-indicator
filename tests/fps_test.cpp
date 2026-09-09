#include "fps_overlay.h"
#include <cmath>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <vector>
using namespace instrument;
int main(){
  FpsCounter counter;
  if(counter.value()>=0)return 1;
  counter.frameCompleted(100);
  for(unsigned i=1;i<=30;++i)counter.frameCompleted(100+i*33334u);
  if(std::fabs(counter.value()-30)>0.01f)return 2;
  // A pause is part of the completed-frame rate, not hidden by render timing.
  for(unsigned i=1;i<=4;++i)counter.frameCompleted(100+1000020+i*250000u);
  if(std::fabs(counter.value()-4)>0.01f)return 3;
  FpsCounter wrapped;
  const uint32_t start=0xffff0000u;
  wrapped.frameCompleted(start);
  for(unsigned i=1;i<=10;++i)wrapped.frameCompleted(start+i*100000u);
  if(std::fabs(wrapped.value()-10)>0.01f)return 4;
  FpsOverlay overlay;
  if(FpsOverlay::X*2+FpsOverlay::Width!=466 || FpsOverlay::Y<420){
    std::puts("FAIL FPS badge must be centered below the instrument");return 12;
  }
  std::vector<uint16_t> frame(466*466+2,0x1234);
  overlay.update(30.0f); overlay.blit(frame.data()+1,466);
  if(frame.front()!=0x1234||frame.back()!=0x1234)return 5;
  unsigned white=0;
  for(int y=0;y<466;++y)for(int x=0;x<466;++x){
    bool inside=x>=FpsOverlay::X&&x<FpsOverlay::X+FpsOverlay::Width&&y>=FpsOverlay::Y&&y<FpsOverlay::Y+FpsOverlay::Height;
    if(!inside && frame[1+y*466+x]!=0x1234)return 6;
    if(inside){float dx=x-232.5f,dy=y-232.5f;if(dx*dx+dy*dy>232.5f*232.5f)return 7;}
    if(frame[1+y*466+x]==0xffff)++white;
  }
  if(white<100)return 8;
  overlay.update(NAN);overlay.update(INFINITY);overlay.update(-1);overlay.update(1000);
  // A tightly cropped destination must never form a global-X pointer first.
  std::vector<uint16_t> cropped(FpsOverlay::Width*FpsOverlay::Height+2,0x1234);
  overlay.blitBand(cropped.data()+1,FpsOverlay::Width,FpsOverlay::Y,FpsOverlay::Height,FpsOverlay::X);
  if(cropped.front()!=0x1234||cropped.back()!=0x1234)return 9;
  if(!std::equal(cropped.begin()+1,cropped.end()-1,overlay.pixels()))return 10;
  std::vector<uint16_t> invalid(466*466+2,0x1234);
  for(int column:{INT_MIN,-1,INT_MAX}){
    overlay.blitBand(invalid.data()+1,466,FpsOverlay::Y,FpsOverlay::Height,column);
    overlay.blitDemoBand(invalid.data()+1,466,414,14,column);
  }
  overlay.blitBand(invalid.data()+1,FpsOverlay::Width,FpsOverlay::Y,FpsOverlay::Height,FpsOverlay::X+1);
  overlay.blitBand(invalid.data()+1,FpsOverlay::Width-1,FpsOverlay::Y,FpsOverlay::Height,FpsOverlay::X);
  overlay.blitDemoBand(invalid.data()+1,48,414,14,210);
  overlay.blitDemoBand(invalid.data()+1,47,414,14,209);
  for(auto pixel:invalid)if(pixel!=0x1234)return 11;
  std::puts("PASS actual completed-frame FPS, stalls, timer wrap, badge text, circular and compact bounds");
}
