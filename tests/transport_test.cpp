#include "pixel_transport.h"
#include <vector>
#include <cstdio>
int main(){
 for(unsigned shift=0;shift<2;++shift)for(unsigned count: {0u,1u,2u,8191u,8192u,8328u}){
  std::vector<uint16_t> src(count+4);std::vector<uint8_t> dst(count*2+16,0xA5);
  for(unsigned i=0;i<count;++i)src[i+shift]=uint16_t(i*3137u+0xF800);
  uint8_t* output=dst.data()+4+shift;
  instrument::copyBigEndian(output,src.data()+shift,count);
  for(unsigned i=0;i<count;++i)if(output[i*2]!=(src[i+shift]>>8)||output[i*2+1]!=(src[i+shift]&255))return 1;
  for(unsigned i=0;i<4+shift;++i)if(dst[i]!=0xA5)return 2;
  for(unsigned i=4+shift+count*2;i<dst.size();++i)if(dst[i]!=0xA5)return 3;
 }
 // Direct-DMA bands change byte order in place after rendering.
 for(unsigned count:{1u,2u,466u*80u,466u*66u}){
  std::vector<uint16_t> pixels(count);for(unsigned i=0;i<count;++i)pixels[i]=uint16_t(i*713u);
  auto original=pixels;
  instrument::copyBigEndian(reinterpret_cast<uint8_t*>(pixels.data()),pixels.data(),count);
  auto bytes=reinterpret_cast<uint8_t*>(pixels.data());
  for(unsigned i=0;i<count;++i)if(bytes[i*2]!=(original[i]>>8)||bytes[i*2+1]!=(original[i]&255))return 4;
 }
 std::puts("PASS transport endian conversion, aligned/unaligned, odd tails, guards");
}
