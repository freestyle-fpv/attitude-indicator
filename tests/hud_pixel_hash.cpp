#include "hud_renderer.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace instrument;
int main(int argc,char**){
  const bool excludeRequestedUi=argc>1;
  HudRenderer renderer;assert(renderer.begin());
  std::vector<uint16_t> frame(466*466);
  unsigned seed=43;
  auto next=[&](){seed=1664525*seed+1013904223;return float(seed>>8)/16777216;};
  for(int i=0;i<500;++i){
    HudInput input;
    input.pitch=next()*180-90;
    input.roll=i<20?(i-10)*.0001f:next()*360-180;
    input.heading=next()*360;input.speedKmh=next()*200;
    input.altitudeM=next()*3000;input.nowMs=i*63;
    renderer.render(frame.data(),input);
    unsigned long long hash=14695981039346656037ull;
    for(size_t j=0;j<frame.size();++j){
      const int x=j%466,y=j/466;
      // Only the requested animation/opacity changes are excluded. Geometry,
      // pitch, bank, center marker and scale strokes remain in the comparison.
      const bool readout=y>=218&&y<250&&((x>=7&&x<74)||(x>=392&&x<459));
      const bool scaleLabel=(y>=125&&y<343&&(x<67||x>=400))||
                            (y>=29&&y<60&&x>=129&&x<=337);
      if(excludeRequestedUi&&(readout||scaleLabel))continue;
      hash^=frame[j];hash*=1099511628211ull;
    }
    std::printf("%llu\n",hash);
  }
}
