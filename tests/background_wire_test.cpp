#include "renderer.h"
#include "fps_overlay.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <limits>
#include <vector>

static uint16_t decode(uint16_t color){return static_cast<uint16_t>((color>>8)|(color<<8));}
int main(){
  const int n=466,pixels=n*n;
  instrument::Renderer native,wire(true);
  instrument::FpsOverlay nativeOverlay,wireOverlay(true);
  assert(native.begin() && wire.begin() && wire.begin());
  std::vector<uint16_t> full(pixels),encoded(pixels+4),band(pixels+4);
  uint32_t state=0x81bd37f2;int cases=0;
  auto random=[&](){state=state*1664525+1013904223;return state*(1./4294967296.);};
  auto compare=[&](float pitch,float roll,int height){
    const int offset=1+(cases&1);
    uint16_t* output=encoded.data()+offset;
    std::fill(encoded.begin(),encoded.end(),0x1234);
    native.render(full.data(),pitch,roll);wire.render(output,pitch,roll);
    for(int i=0;i<pixels;++i)assert(full[i]==decode(output[i]));
    assert(output[-1]==0x1234 && output[pixels]==0x1234);
    const float fps=(cases%5==0)?-1:cases*.13f;
    nativeOverlay.update(fps);wireOverlay.update(fps);wireOverlay.update(fps);
    for(int i=0;i<wireOverlay.Width*wireOverlay.Height;++i)assert(nativeOverlay.pixels()[i]==decode(wireOverlay.pixels()[i]));
    nativeOverlay.blit(full.data(),n);wireOverlay.blit(output,n);
    nativeOverlay.blitDemoBand(full.data(),n,0,n);wireOverlay.blitDemoBand(output,n,0,n);
    for(int i=0;i<pixels;++i)assert(full[i]==decode(output[i]));
    for(int first=0;first<n;first+=height){
      const int rows=std::min(height,n-first);
      uint16_t* row=band.data()+offset;
      std::fill(band.begin(),band.end(),0x1234);
      wire.renderBand(row,pitch,roll,first,rows);
      wireOverlay.blitBand(row,n,first,rows);
      wireOverlay.blitDemoBand(row,n,first,rows);
      for(int i=0;i<rows*n;++i)assert(full[first*n+i]==decode(row[i]));
      assert(row[-1]==0x1234 && row[rows*n]==0x1234);
    }
    ++cases;
  };
  const float pitches[]={-100,-90,-89.999f,-60,-1,0,1,60,89.999f,90,100};
  const float rolls[]={-180,-90,-89.999f,-.001f,0,.001f,89.999f,90,180};
  const int heights[]={1,37,79,80,110,118,466};
  for(float pitch:pitches)for(float roll:rolls)compare(pitch,roll,heights[cases%7]);
  for(int i=0;i<100;++i)compare(random()*220-110,random()*2160-1080,118);
  compare(std::numeric_limits<float>::quiet_NaN(),0,80);
  compare(0,std::numeric_limits<float>::infinity(),80);
  std::fill(band.begin(),band.end(),0x1234);
  const int invalid[][2]={{-1,1},{466,1},{0,0},{0,-1},{0,467},{465,2},{INT_MAX,1},{0,INT_MAX}};
  for(const auto& range:invalid)wire.renderBand(band.data()+1,0,0,range[0],range[1]);
  wire.renderBand(nullptr,0,0,0,80);
  for(uint16_t pixel:band)assert(pixel==0x1234);
  instrument::Renderer notStarted(true);
  notStarted.renderBand(band.data()+1,0,0,100,3);
  for(int i=1;i<=3*n;++i)assert(band[i]==0);
  assert(band.front()==0x1234 && band[3*n+1]==0x1234);
  // The label writes glyph pixels only, including when a band splits a glyph.
  std::vector<uint16_t> demo(n*n,0x1357),demoParts(n*n,0x1357);
  nativeOverlay.blitDemoBand(demo.data(),n,0,n);
  for(int first=0;first<n;first+=7)nativeOverlay.blitDemoBand(demoParts.data()+first*n,n,first,std::min(7,n-first));
  assert(demo==demoParts);
  size_t yellow=0;
  for(int y=0;y<n;++y)for(int x=0;x<n;++x){
    const uint16_t color=demo[y*n+x];
    if(color==0xffe0){assert(x>=209 && x<257 && y>=414 && y<428);++yellow;}
    else assert(color==0x1357);
  }
  assert(yellow>100 && yellow<48*14);
  std::fill(band.begin(),band.end(),0x1234);
  wireOverlay.blitDemoBand(band.data()+1,n,428,INT_MAX);
  wireOverlay.blitDemoBand(band.data()+1,256,414,14);
  wireOverlay.blitDemoBand(band.data()+1,n,-1,14);
  wireOverlay.blitDemoBand(nullptr,n,414,14);
  for(uint16_t pixel:band)assert(pixel==0x1234);
  std::printf("Wire renderer: %d poses decode exactly to native full/bands/FPS/DEMO; guards, transparent label, invalid inputs and repeated begin/update PASS\n",cases);
}
