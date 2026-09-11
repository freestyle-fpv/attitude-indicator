#pragma once
namespace instrument {
enum class FrameDecision { Draw,Status,Discard };
// Airspeed and altimeter have independent simulated sources. IMU validity only authorizes
// real attitude frames; a freshly recovered IMU must not display an old pose.
inline FrameDecision decideFrame(bool simulatedDial,bool attitude,bool demo,bool current,bool jobFresh){
  if(simulatedDial)return FrameDecision::Draw;
  if(attitude&&!demo&&current&&!jobFresh)return FrameDecision::Discard;
  return attitude&&(demo||current)?FrameDecision::Draw:FrameDecision::Status;
}
// Invalid HUD poses are explicitly drawn without attitude symbology. Drop any
// previously valid pose that became stale while its raster work was queued.
inline FrameDecision decideHudFrame(bool demo,bool current,bool jobFresh,bool preparedValid){
  return !demo&&preparedValid&&(!current||!jobFresh)?FrameDecision::Discard:FrameDecision::Draw;
}
}
