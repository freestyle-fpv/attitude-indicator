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
  overlay.blitBand(invalid.data()+1,0,FpsOverlay::Y,FpsOverlay::Height,FpsOverlay::X);
  overlay.blitBand(invalid.data()+1,-1,FpsOverlay::Y,FpsOverlay::Height,FpsOverlay::X);
  overlay.blitDemoBand(invalid.data()+1,0,414,14,209);
  for(auto pixel:invalid)if(pixel!=0x1234)return 11;
  std::vector<uint16_t> sim(466*466,0x1234),simWire(466*466,0x3412);
  FpsOverlay wire(true);
  overlay.blitSimBand(sim.data(),466,0,466);
  for(int y=0;y<466;y+=37)wire.blitSimBand(simWire.data()+y*466,466,y,std::min(37,466-y));
  unsigned changed=0;
  for(int y=0;y<466;++y)for(int x=0;x<466;++x){
    const auto value=sim[y*466+x];
    if(value!=0x1234){
      ++changed;if(x<215||x>=251||y<414||y>=428)return 13;
    }
    if(simWire[y*466+x]!=static_cast<uint16_t>((value<<8)|(value>>8)))return 14;
  }
  if(changed<100)return 15;
  // A needle patch can intersect only part of a badge. Horizontal clipping
  // must reconstruct the same label as a full-frame overlay, with no remnants.
  overlay.blitBand(sim.data(),466,0,466);
  for(int y=400;y<466;y+=11)for(int x=170;x<290;x+=17){
    const int width=std::min(17,290-x),height=std::min(11,466-y);
    std::vector<uint16_t> patch(width*height+2,0x1234);
    overlay.blitBand(patch.data()+1,width,y,height,x);
    overlay.blitSimBand(patch.data()+1,width,y,height,x);
    if(patch.front()!=0x1234||patch.back()!=0x1234)return 16;
    for(int row=0;row<height;++row)for(int col=0;col<width;++col)
      if(patch[1+row*width+col]!=sim[(y+row)*466+x+col]){
        std::puts("FAIL partially covered FPS/SIM label was not restored");return 17;
      }
  }
  // The opaque SIM backing hides every prior tick/needle pixel, including
  // partially covered label edges, in both native and panel byte order.
  overlay.blitSimBand(sim.data(),466,0,466,0,true);
  std::fill(simWire.begin(),simWire.end(),0x3412);
  wire.update(1000);wire.blitBand(simWire.data(),466,0,466);
  wire.blitSimBand(simWire.data(),466,0,466,0,true);
  for(int y=400;y<432;y+=7)for(int x=200;x<264;x+=9){
    const int width=std::min(9,264-x),height=std::min(7,432-y);
    std::vector<uint16_t> patch(width*height+2,0x1234);
    overlay.blitSimBand(patch.data()+1,width,y,height,x,true);
    if(patch.front()!=0x1234||patch.back()!=0x1234)return 18;
    for(int row=0;row<height;++row)for(int col=0;col<width;++col){
      const auto value=sim[(y+row)*466+x+col];
      if(patch[1+row*width+col]!=value)return 19;
      if(simWire[(y+row)*466+x+col]!=static_cast<uint16_t>((value<<8)|(value>>8)))return 20;
    }
  }
  for(int y=411;y<431;++y)for(int x=211;x<255;++x)
    if(sim[y*466+x]!=0x0841&&sim[y*466+x]!=0xffe0)return 21;
  std::puts("PASS actual completed-frame FPS, stalls, timer wrap, badge text, circular and compact bounds");
}
