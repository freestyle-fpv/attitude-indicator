#include "diagnostics.h"
#include <cstdio>
struct Sink {
  int capacity, calls=0; unsigned timeout=100;
  void setTxTimeoutMs(unsigned ms){timeout=ms;}
  int availableForWrite(){return capacity;}
  size_t write(const uint8_t*,size_t size){++calls;return size;}
};
int main(){
  Sink sink; sink.capacity=0;
  instrument::configureDiagnostics(sink);
  for(int i=0;i<10000;++i)
    if(instrument::tryDiagnostic(sink,"sample\n",7)||sink.calls||sink.timeout)return 1;
  sink.capacity=7;
  if(!instrument::tryDiagnostic(sink,"sample\n",7)||sink.calls!=1)return 1;
  sink.capacity=6;
  if(instrument::tryDiagnostic(sink,"sample\n",7)||sink.calls!=1)return 1;
  std::puts("PASS diagnostics never write to a full or insufficient buffer, timeout is zero");
}
