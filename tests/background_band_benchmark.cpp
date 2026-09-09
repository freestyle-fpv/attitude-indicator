#include "renderer.h"
#undef INSTRUMENT_RENDERER_H
#define instrument background_reference
#include "fixtures/background_band_benchmark_reference/renderer.h"
#undef instrument
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>

int main(){
  instrument::Renderer actual;background_reference::Renderer reference;
  assert(actual.begin() && reference.begin());
  std::vector<uint16_t> pixels(466*80);
  double actualMs=0,referenceMs=0;
  const int cases=3000;
  for(int i=0;i<cases;++i){
    const float pitch=(i%181)-90,roll=(i*37)%720-360;
    auto start=std::chrono::steady_clock::now();
    if(i&1){
      for(int row=0;row<466;row+=80)actual.renderBand(pixels.data(),pitch,roll,row,std::min(80,466-row));
    }else{
      for(int row=0;row<466;row+=80)reference.renderBand(pixels.data(),pitch,roll,row,std::min(80,466-row));
    }
    auto middle=std::chrono::steady_clock::now();
    if(i&1){
      for(int row=0;row<466;row+=80)reference.renderBand(pixels.data(),pitch,roll,row,std::min(80,466-row));
    }else{
      for(int row=0;row<466;row+=80)actual.renderBand(pixels.data(),pitch,roll,row,std::min(80,466-row));
    }
    auto end=std::chrono::steady_clock::now();
    const double first=std::chrono::duration<double,std::milli>(middle-start).count();
    const double second=std::chrono::duration<double,std::milli>(end-middle).count();
    actualMs+=(i&1)?first:second;referenceMs+=(i&1)?second:first;
  }
  std::printf("%d frames of six 80-row bands: optimized %.4f ms, reference %.4f ms, improvement %.1f%%\n",cases,actualMs/cases,referenceMs/cases,(referenceMs-actualMs)*100/referenceMs);
}
