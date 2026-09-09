#pragma once
#include <cstddef>
#include <cstdint>
namespace instrument {
inline void copyBigEndian(uint8_t* target,const uint16_t* source,size_t count){
#if defined(__GNUC__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  // GCC/Clang may_alias allows packed word access to RGB565 without violating
  // strict aliasing. Only use words when BOTH addresses are aligned.
  typedef uint32_t AliasWord __attribute__((__may_alias__));
  if(((reinterpret_cast<uintptr_t>(target)|reinterpret_cast<uintptr_t>(source))&3u)==0){
    auto* out=reinterpret_cast<AliasWord*>(target);
    auto* in=reinterpret_cast<const AliasWord*>(source);
    const size_t pairs=count/2;
    for(size_t i=0;i<pairs;++i){uint32_t p=in[i];out[i]=((p>>8)&0x00ff00ffu)|((p<<8)&0xff00ff00u);}
    source+=pairs*2;target+=pairs*4;count-=pairs*2;
  }
#endif
  while(count--){uint16_t p=*source++;*target++=p>>8;*target++=p&255;}
}
}
