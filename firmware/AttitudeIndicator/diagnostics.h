#pragma once
#include <cstddef>
#include <cstdint>
namespace instrument {
template<class Sink> void configureDiagnostics(Sink& sink){sink.setTxTimeoutMs(0);}
template<class Sink> bool tryDiagnostic(Sink& sink,const char* text,size_t size){
  if(!text||!size||sink.availableForWrite()<static_cast<int>(size))return false;
  return sink.write(reinterpret_cast<const uint8_t*>(text),size)==size;
}
}
