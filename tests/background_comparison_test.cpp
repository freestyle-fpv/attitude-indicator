#include "renderer.h"
#undef INSTRUMENT_RENDERER_H
#define instrument background_reference
#include "fixtures/background_reference/renderer.h"
#undef instrument
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <vector>

#if defined(INSTRUMENT_TEST_CACHE)
namespace instrument {
extern int backgroundAllocationFailure;
extern size_t backgroundCacheStats[5];
}
#endif
int main(){
#if defined(INSTRUMENT_TEST_CACHE)
  for(int allocation=0;allocation<6;++allocation){
    instrument::Renderer retry;
    instrument::backgroundAllocationFailure=allocation;
    assert(!retry.begin());
    std::vector<uint16_t> blank(466*466,0x1234);
    retry.render(blank.data(),0,0);
    for(uint16_t pixel:blank)assert(pixel==0);
    instrument::backgroundAllocationFailure=-1;
    assert(retry.begin());
  }
  std::puts("All six allocation failures cleanly reject initialization and allow retry");
#endif
  instrument::Renderer actual;
  background_reference::Renderer expected;
  assert(actual.begin() && expected.begin());
#if defined(INSTRUMENT_TEST_CACHE)
  const size_t* stats=instrument::backgroundCacheStats;
  std::printf("fixed cache: %zu original ops -> %zu edge ops + %zu spans covering %zu opaque pixels; %zu bytes\n",stats[0],stats[1],stats[2],stats[4],stats[3]);
#endif
  const int pixels=466*466;
  // Offset by one RGB565 pixel to exercise a valid two-byte-aligned destination.
  std::vector<uint16_t> a(pixels+4,0x1234),b(pixels);
  uint32_t state=0x8ba539e1;
  auto random=[&](){state=1664525*state+1013904223;return state*(1./4294967296.);};
  size_t changed=0,totalChannelError=0;unsigned maxChannelError=0;int cases=0;
  double actualMs=0,referenceMs=0;
  auto compare=[&](float pitch,float roll){
    std::fill(a.begin(),a.end(),0x1234);
    uint16_t* output=a.data()+1+(cases&1);
    auto start=std::chrono::steady_clock::now();
    actual.render(output,pitch,roll);
    auto middle=std::chrono::steady_clock::now();
    expected.render(b.data(),pitch,roll);
    auto end=std::chrono::steady_clock::now();
    actualMs+=std::chrono::duration<double,std::milli>(middle-start).count();
    referenceMs+=std::chrono::duration<double,std::milli>(end-middle).count();
    assert(a.front()==0x1234 && a.back()==0x1234);
    assert(output[-1]==0x1234 && output[pixels]==0x1234);
    for(int i=0;i<pixels;++i){
      if(output[i]==b[i])continue;
      if(changed<8)std::printf("difference pitch=%g roll=%g pixel=%d actual=%04x reference=%04x\n",pitch,roll,i,output[i],b[i]);
      ++changed;
      for(int channel=0;channel<3;++channel){
        const int shift=channel==0?11:channel==1?5:0,mask=channel==1?63:31;
        const unsigned error=std::abs(int((output[i]>>shift)&mask)-int((b[i]>>shift)&mask));
        totalChannelError+=error;
        if(error>maxChannelError)maxChannelError=error;
      }
    }
    ++cases;
  };
  const float pitches[]={-100,-90,-89.999f,-89,-60,-30,-1,-.001f,0,.001f,1,30,60,89,89.999f,90,100};
  const float rolls[]={-360,-270.001f,-270,-180.001f,-180,-90.001f,-90,-89.999f,-45,-.001f,0,.001f,45,89.999f,90,90.001f,180,270,360};
  for(float pitch:pitches)for(float roll:rolls)compare(pitch,roll);
  for(int i=0;i<1000;++i)compare(random()*220-110,random()*2160-1080);
  compare(std::numeric_limits<float>::quiet_NaN(),0);
  compare(0,std::numeric_limits<float>::infinity());
  std::printf("%d poses, %zu changed pixels, maximum RGB565 channel error %u\n",cases,changed,maxChannelError);
  std::printf("mean summed RGB565 channel error per pixel: %.9f\n",double(totalChannelError)/(cases*size_t(pixels)));
  std::printf("native mean render: optimized %.3f ms, reference %.3f ms\n",actualMs/cases,referenceMs/cases);
#if defined(INSTRUMENT_TEST_LINE_ROUNDING)
  assert(maxChannelError<=1 && double(totalChannelError)/(cases*size_t(pixels))<.01);
#else
  assert(changed==0);
#endif
}
