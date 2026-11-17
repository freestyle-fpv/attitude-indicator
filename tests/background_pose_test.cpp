#include "renderer.h"
#undef INSTRUMENT_RENDERER_H
#define instrument background_reference
#include "fixtures/background_pose_reference/renderer.h"
#undef instrument
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <vector>

namespace instrument {
extern size_t backgroundPoseBuilds,backgroundPoseCommands,backgroundPoseOverflows,backgroundPoseBankOverflows;
}
int main(){
  const int n=466,pixels=n*n;
  instrument::Renderer actual(true);
  background_reference::Renderer reference(true);
  assert(actual.begin() && reference.begin());
  std::vector<uint16_t> full(pixels),composed(pixels),a(n*118+4),b(n*118);
  uint32_t state=0x89516abc;int cases=0;size_t largest=0;
  double actualMs=0,referenceMs=0;
  auto random=[&](){state=state*1664525+1013904223;return state*(1./4294967296.);};
  auto compare=[&](float pitch,float roll){
    reference.render(full.data(),pitch,roll);
    for(int first=0;first<n;first+=118){
      const int rows=std::min(118,n-first),offset=1+(cases&1);
      std::fill(a.begin(),a.end(),0x1234);
      const size_t before=instrument::backgroundPoseBuilds;
      auto start=std::chrono::steady_clock::now();
      actual.renderBand(a.data()+offset,pitch,roll,first,rows);
      auto middle=std::chrono::steady_clock::now();
      reference.renderBand(b.data(),pitch,roll,first,rows);
      auto end=std::chrono::steady_clock::now();
      actualMs+=std::chrono::duration<double,std::milli>(middle-start).count();
      referenceMs+=std::chrono::duration<double,std::milli>(end-middle).count();
      if(first)assert(instrument::backgroundPoseBuilds==before);
      assert(a[offset-1]==0x1234 && a[offset+rows*n]==0x1234);
      for(int i=0;i<rows*n;++i)assert(a[offset+i]==b[i] && a[offset+i]==full[first*n+i]);
      std::copy(a.begin()+offset,a.begin()+offset+rows*n,composed.begin()+first*n);
      largest=std::max(largest,instrument::backgroundPoseCommands);
    }
    const size_t before=instrument::backgroundPoseBuilds;
    actual.render(composed.data(),pitch,roll);
    assert(instrument::backgroundPoseBuilds==before && composed==full);
    ++cases;
  };
  const float pitches[]={-100,-90,-89.999f,-60,-1,0,1,60,89.999f,90,100};
  const float rolls[]={-180,-90,-89.999f,-.001f,0,.001f,89.999f,90,180};
  for(float pitch:pitches)for(float roll:rolls)compare(pitch,roll);
  for(int i=0;i<100;++i)compare(random()*220-110,random()*2160-1080);
  // Invalidate between arbitrary, non-sequential bands and return to an old pose.
  for(int i=0;i<12;++i){
    const float pitch=(i%2)?31.f:-22.f,roll=(i%2)?71.f:-103.f;
    const int first=(i%3)*118;
    actual.renderBand(a.data()+1,pitch,roll,first,118);
    reference.renderBand(b.data(),pitch,roll,first,118);
    for(int j=0;j<n*118;++j)assert(a[j+1]==b[j]);
  }
  const size_t before=instrument::backgroundPoseBuilds;
  actual.renderBand(a.data()+1,std::numeric_limits<float>::quiet_NaN(),0,0,118);
  assert(instrument::backgroundPoseBuilds==before);
  for(int i=0;i<n*118;++i)assert(a[i+1]==0);
  std::printf("Pose cache: %d full/118-row poses exactly match uncached fixture; max %zu commands; %zu builds; %zu safe overflows (%zu during bank)\n",cases,largest,instrument::backgroundPoseBuilds,instrument::backgroundPoseOverflows,instrument::backgroundPoseBankOverflows);
  std::printf("Mean four-band frame: cached %.4f ms, uncached %.4f ms\n",actualMs/cases,referenceMs/cases);
#if INSTRUMENT_COMMAND_CAPACITY <= 229
  assert(instrument::backgroundPoseOverflows>0);
#elif INSTRUMENT_COMMAND_CAPACITY == 1024
  assert(instrument::backgroundPoseOverflows==0);
#endif
#if INSTRUMENT_COMMAND_CAPACITY == 229
  assert(instrument::backgroundPoseBankOverflows>0);
#endif
}
