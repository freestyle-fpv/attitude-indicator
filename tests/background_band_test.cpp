#include "renderer.h"
#include "fps_overlay.h"
#undef INSTRUMENT_RENDERER_H
#define instrument background_reference
#include "fixtures/background_band_reference/renderer.h"
#undef instrument
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

int main(){
  const int n=466,pixels=n*n;
  instrument::Renderer renderer;
  background_reference::Renderer reference;
  instrument::FpsOverlay overlay;
  assert(renderer.begin() && reference.begin());
  std::vector<uint16_t> full(pixels),expected(pixels),composed(pixels),guarded(pixels+4);
  uint32_t state=0x3279cdba;int cases=0;
  auto random=[&](){state=state*1664525+1013904223;return state*(1./4294967296.);};
  auto compare=[&](float pitch,float roll,int height){
    renderer.render(full.data(),pitch,roll);
    reference.render(expected.data(),pitch,roll);
    assert(full==expected);
    overlay.update(cases*.13f);
    overlay.blit(full.data(),n);
    // Compare the full wrapper with the former direct overlay placement.
    for(int y=0;y<overlay.Height;++y)
      std::memcpy(expected.data()+(overlay.Y+y)*n+overlay.X,overlay.pixels()+y*overlay.Width,overlay.Width*sizeof(uint16_t));
    assert(full==expected);
    for(int first=0;first<n;first+=height){
      const int rows=std::min(height,n-first),offset=1+(cases&1);
      std::fill(guarded.begin(),guarded.end(),0x1234);
      uint16_t* band=guarded.data()+offset;
      renderer.renderBand(band,pitch,roll,first,rows);
      overlay.blitBand(band,n,first,rows);
      assert(band[-1]==0x1234 && band[rows*n]==0x1234);
      std::copy(band,band+rows*n,composed.begin()+first*n);
    }
    assert(full==composed);++cases;
  };
  const float pitches[]={-90,-89.999f,-60,-1,0,1,60,89.999f,90};
  const float rolls[]={-180,-90,-89.999f,-.001f,0,.001f,89.999f,90,180};
  const int heights[]={1,37,79,80,110,118,466};
  for(float pitch:pitches)for(float roll:rolls)compare(pitch,roll,heights[cases%7]);
  for(int i=0;i<100;++i)compare(random()*220-110,random()*2160-1080,118);
  compare(std::numeric_limits<float>::quiet_NaN(),0,80);
  compare(0,std::numeric_limits<float>::infinity(),80);

  std::fill(guarded.begin(),guarded.end(),0x1234);
  const int invalid[][2]={{-1,1},{466,1},{467,1},{0,0},{0,-1},{0,467},{465,2},{INT_MAX,1},{0,INT_MAX},{INT_MIN,INT_MAX}};
  for(const auto& range:invalid)renderer.renderBand(guarded.data()+1,0,0,range[0],range[1]);
  renderer.renderBand(nullptr,0,0,0,80);
  for(uint16_t pixel:guarded)assert(pixel==0x1234);
  instrument::Renderer uninitialized;
  uninitialized.renderBand(guarded.data()+1,0,0,22,3);
  for(int i=1;i<=3*n;++i)assert(guarded[i]==0);
  assert(guarded.front()==0x1234 && guarded[3*n+1]==0x1234);

  // A padded row stride and a band boundary through the FPS text stay exact.
  const int stride=512;
  std::vector<uint16_t> padded(stride*466,0x5678),parts(stride*466,0x5678);
  overlay.blit(padded.data(),stride);
  for(int first=0;first<466;first+=11)overlay.blitBand(parts.data()+first*stride,stride,first,std::min(11,466-first));
  assert(padded==parts);
  overlay.blitBand(nullptr,n,0,80);
  std::fill(guarded.begin(),guarded.end(),0x1234);
  // A narrow band can overlap the overlay; this one ends just before it.
  overlay.blitBand(guarded.data()+1,overlay.X,0,466);
  overlay.blitBand(guarded.data()+1,n,-1,466);
  overlay.blitBand(guarded.data()+1,n,0,0);
  overlay.blitBand(guarded.data()+1,n,INT_MAX,INT_MAX);
  for(uint16_t pixel:guarded)assert(pixel==0x1234);
  std::printf("Band renderer: %d poses, exact full-frame/reference/band/overlay equality; guards and invalid ranges PASS\n",cases);
}
