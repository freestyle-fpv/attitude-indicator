#include "airspeed_renderer.h"
#include "circle_band.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using instrument::AirspeedRenderer;
constexpr int N=466;
constexpr uint16_t Guard=0xa55a;
int main(){
  AirspeedRenderer renderer,wire(true);
  assert(renderer.begin()&&wire.begin());
  std::vector<uint16_t> reference(N*N),actual(N*N),encoded(N*N);
  // Wrong range handling would move the pointer beyond the two end stops.
  renderer.render(reference.data(),0);
  renderer.render(actual.data(),-50);assert(reference==actual);
  renderer.render(reference.data(),200);
  renderer.render(actual.data(),250);assert(reference==actual);
  // A broken pointer transform must not leave every speed showing the same face.
  assert(std::count_if(reference.begin(),reference.end(),[](uint16_t p){return p!=0;})>160000);
  renderer.render(actual.data(),0);assert(reference!=actual);
  // Hand-checked points 100 pixels from the hub at the start, middle and end.
  // Reversing the sweep or using the old 300 km/h mapping must fail here.
  const float probes[]={0,100,200};const int px[]={162,233,303},py[]={303,133,303};
  for(int p=0;p<3;++p){
    renderer.render(actual.data(),probes[p]);uint16_t color=actual[py[p]*N+px[p]];
    assert((color>>11)>=30&&((color>>5)&63)>=60&&(color&31)>=24);
  }
  for(float speed:{0.f,20.f,95.f,135.f,180.f,200.f}){
    renderer.render(reference.data(),speed);wire.render(encoded.data(),speed);
    for(int i=0;i<N*N;++i)assert(encoded[i]==static_cast<uint16_t>((reference[i]<<8)|(reference[i]>>8)));
    // These exercise arbitrary compact strides, including partial final bands.
    for(int height:{1,37,60,119,466}){
      std::fill(actual.begin(),actual.end(),0);
      for(int y=0;y<N;y+=height){
        int rows=std::min(height,N-y);auto band=instrument::circleBandBounds(y,rows);
        std::vector<uint16_t> storage(rows*band.width+2,Guard);
        renderer.renderBackgroundBand(storage.data()+1,y,rows,true);
        renderer.renderForegroundBand(storage.data()+1,speed,y,rows,true);
        assert(storage.front()==Guard&&storage.back()==Guard);
        for(int r=0;r<rows;++r)std::copy_n(storage.data()+1+r*band.width,band.width,actual.data()+(y+r)*N+band.x);
      }
      // Only actual circle pixels are transferred by the existing DMA bounds.
      for(int y=0;y<N;++y)for(int x=0;x<N;++x)
        if((2*x-465)*(2*x-465)+(2*y-465)*(2*y-465)<=465*465)assert(actual[y*N+x]==reference[y*N+x]);
    }
  }
  // Partial updates must erase the previous pointer and its soft shadow exactly.
  for(AirspeedRenderer* r:{&renderer,&wire}){
    float previous=0;r->render(actual.data(),previous);
    for(float next:{1.f,20.f,95.f,135.f,180.f,200.f,0.f,100.f,99.9f}){
      const auto rect=AirspeedRenderer::damage(previous,next);
      assert(rect.x>=0&&rect.y>=0&&rect.width>0&&rect.height>0&&rect.x+rect.width<=N&&rect.y+rect.height<=N);
      assert(rect.x%2==0&&rect.y%2==0&&rect.width%2==0&&rect.height%2==0);
      std::vector<uint16_t> patch(rect.width*rect.height+2,Guard);
      r->renderRectangle(patch.data()+1,next,rect);
      assert(patch.front()==Guard&&patch.back()==Guard);
      for(int y=0;y<rect.height;++y)std::copy_n(patch.data()+1+y*rect.width,rect.width,actual.data()+(rect.y+y)*N+rect.x);
      r->render(reference.data(),next);assert(actual==reference);previous=next;
    }
  }
  std::fill(actual.begin(),actual.end(),Guard);
  renderer.renderBackgroundBand(actual.data(),-1,60);
  renderer.renderForegroundBand(actual.data(),100,465,2);
  renderer.renderBackgroundBand(actual.data(),0,std::numeric_limits<int>::max());
  renderer.renderForegroundBand(nullptr,100,0,60);
  assert(std::all_of(actual.begin(),actual.end(),[](uint16_t p){return p==Guard;}));
  renderer.render(actual.data(),std::numeric_limits<float>::quiet_NaN());
  renderer.renderBackgroundBand(reference.data(),0,N);
  assert(actual==reference); // Invalid data must not produce an arbitrary pointer.
  std::puts("PASS airspeed range, moving pointer, wire order, compact bands, invalid input and buffer guards");
}
