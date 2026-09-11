#include "display_policy.h"
#include <cassert>
#include <cstdio>
int main(){
  using instrument::FrameDecision;using instrument::decideFrame;
  assert(decideFrame(true,false,false,false,false)==FrameDecision::Draw);
  assert(decideFrame(true,false,true,false,false)==FrameDecision::Draw);
  assert(decideFrame(false,true,false,true,true)==FrameDecision::Draw);
  assert(decideFrame(false,true,false,true,false)==FrameDecision::Discard);
  assert(decideFrame(false,true,false,false,true)==FrameDecision::Status);
  assert(decideFrame(false,true,true,false,false)==FrameDecision::Draw);
  assert(decideFrame(false,false,false,false,false)==FrameDecision::Status);
  assert(instrument::decideHudFrame(false,false,false,false)==FrameDecision::Draw);
  assert(instrument::decideHudFrame(false,false,true,true)==FrameDecision::Discard);
  assert(instrument::decideHudFrame(false,true,false,true)==FrameDecision::Discard);
  assert(instrument::decideHudFrame(false,true,true,true)==FrameDecision::Draw);
  assert(instrument::decideHudFrame(true,false,false,true)==FrameDecision::Draw);
  std::puts("PASS airspeed source independence and attitude stale/fault/demo display policy");
}
