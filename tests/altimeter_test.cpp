#include "altimeter_renderer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include <thread>
using namespace instrument;
constexpr int N=466;
constexpr uint16_t Guard=0xa55a;
int main(){
  AltimeterRenderer r,wire(true);assert(r.begin()&&wire.begin());
  std::vector<uint16_t> reference(N*N),actual(N*N),encoded(N*N),face(N*N);
  r.renderBackgroundRectangle(face.data(),{0,0,N,N});
  // Independent hand-checked points catch reversed rotation or wrong hand ratios.
  struct Probe{float altitude;int x,y;};
  const Probe probes[]={{25,372,232},{50,232,372},{75,92,232},{250,317,232},{2500,432,232}};
  for(auto p:probes){r.render(actual.data(),p.altitude);auto c=actual[p.y*N+p.x];assert(c!=face[p.y*N+p.x]);assert((c>>11)>=30&&((c>>5)&63)>=60&&(c&31)>=24);}
  r.render(actual.data(),-10);r.render(reference.data(),0);assert(actual==reference);
  r.render(actual.data(),3001);r.render(reference.data(),3000);assert(actual==reference);
  r.render(actual.data(),0);assert(actual!=reference); // 3000 m must not alias zero.
  r.render(actual.data(),std::numeric_limits<float>::quiet_NaN());assert(actual==face);
  for(float altitude:{0.f,25.f,99.f,100.f,101.f,275.f,999.f,1000.f,2500.f,2999.9f,3000.f}){
    r.render(reference.data(),altitude);wire.render(encoded.data(),altitude);
    for(int i=0;i<N*N;++i)assert(encoded[i]==static_cast<uint16_t>((reference[i]<<8)|(reference[i]>>8)));
  }
  // Reconstruct using only old/new patches, including every pointer's wrap.
  for(AltimeterRenderer* renderer:{&r,&wire}){
    float previous=0;renderer->render(actual.data(),previous);
    for(float next:{1.f,25.f,99.f,100.f,101.f,275.f,750.f,999.f,1000.f,1001.f,2500.f,2999.9f,3000.f,3001.f,0.f}){
      const auto damage=AltimeterRenderer::damage(previous,next);
      assert(damage.count>=1&&damage.count<=AltimeterDamage::Capacity);
      int lastY=0,totalPixels=0;
      for(int i=0;i<damage.count;++i){
        const auto a=damage.regions[i];
        assert(a.x>=0&&a.y>=0&&a.width>0&&a.height>0&&a.x+a.width<=N&&a.y+a.height<=N);
        assert((a.x|a.y|a.width|a.height)%2==0);
        assert(a.y>=lastY);lastY=a.y+a.height;totalPixels+=a.width*a.height;assert(totalPixels<=N*N);
        std::vector<uint16_t> patch(a.width*a.height+2,Guard);
        renderer->renderRectangle(patch.data()+1,next,a);
        assert(patch.front()==Guard&&patch.back()==Guard);
        for(int y=0;y<a.height;++y)std::copy_n(patch.data()+1+y*a.width,a.width,actual.data()+(a.y+y)*N+a.x);
      }
      renderer->render(reference.data(),next);assert(actual==reference);previous=next;
    }
  }
  // Frame slots must preserve their own pose when the other core prepares ahead.
  assert(r.prepareFrame(0,75)&&r.prepareFrame(1,275));
  r.renderBackgroundRectangle(actual.data(),{0,0,N,N});r.renderForegroundPrepared(actual.data(),0,{0,0,N,N});
  r.render(reference.data(),75);assert(actual==reference);
  r.renderBackgroundRectangle(actual.data(),{0,0,N,N});r.renderForegroundPrepared(actual.data(),1,{0,0,N,N});
  r.render(reference.data(),275);assert(actual==reference);
  assert(!r.prepareFrame(1,std::numeric_limits<float>::quiet_NaN()));
  r.renderBackgroundRectangle(actual.data(),{0,0,N,N});r.renderForegroundPrepared(actual.data(),1,{0,0,N,N});assert(actual==face);
  assert(!r.prepareFrame(AltimeterRenderer::FrameSlots,100));
  // Reproduce the two-core pipeline: disjoint regions share one immutable pose,
  // while a different slot may be prepared for the following frame.
  auto damage=AltimeterRenderer::damage(274.6f,275);assert(r.prepareFrame(0,275));
  std::vector<uint16_t> packed(N*N);std::vector<int> offsets;int offset=0;
  for(int i=0;i<damage.count;++i){offsets.push_back(offset);const auto a=damage.regions[i];offset+=a.width*a.height;}
  auto draw=[&](int parity){for(int i=parity;i<damage.count;i+=2){r.renderBackgroundRectangle(packed.data()+offsets[i],damage.regions[i]);r.renderForegroundPrepared(packed.data()+offsets[i],0,damage.regions[i]);}};
  std::thread background([&](){draw(0);});
  r.prepareFrame(2,999);draw(1);background.join();
  r.render(actual.data(),274.6f);
  for(int i=0;i<damage.count;++i){auto a=damage.regions[i];for(int y=0;y<a.height;++y)std::copy_n(packed.data()+offsets[i]+y*a.width,a.width,actual.data()+(a.y+y)*N+a.x);}
  r.render(reference.data(),275);assert(actual==reference);
  std::fill(actual.begin(),actual.end(),Guard);
  for(auto rect:{AirspeedRect{-1,0,2,2},AirspeedRect{0,0,INT32_MAX,2},AirspeedRect{0,465,4,2},AirspeedRect{466,0,1,1}}){
    r.renderRectangle(actual.data(),200,rect);
  }
  assert(std::all_of(actual.begin(),actual.end(),[](uint16_t p){return p==Guard;}));
  std::puts("PASS altimeter: meter hand ratios, limits, finite inputs, wire order, aligned damage, wrap reconstruction and guards");
}
