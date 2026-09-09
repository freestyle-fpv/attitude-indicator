#include "raster_math.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
int main(){
  auto check=[](float v){
    assert(instrument::floorBound(v)==static_cast<int>(std::floor(v)));
    assert(instrument::ceilBound(v)==static_cast<int>(std::ceil(v)));
    assert(instrument::roundBound(v)==std::lround(v));
  };
  for(int i=-1024;i<=1024;++i){
    check(i);check(std::nextafter(float(i),INFINITY));check(std::nextafter(float(i),-INFINITY));
    check(i+.5f);
  }
  uint32_t state=0x832647ab;
  for(int i=0;i<1000000;++i){state=state*1664525u+1013904223u;check((state/4294967296.f)*2048-1024);}
  for(int e=0;e<=28;++e){
    float v=std::ldexp(1.f,e);
    for(int sign : {-1,1}){check(sign*v);check(std::nextafter(sign*v,INFINITY));check(std::nextafter(sign*v,-INFINITY));}
  }
  for(int i=0;i<1000000;++i){state=state*1664525u+1013904223u;check((state/4294967296.f)*536870912.f-268435456.f);}
  std::puts("PASS finite raster bounds match floor/ceil/lround at integer edges, large Q18 powers, and 2M random coordinates");
}
