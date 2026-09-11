#pragma once
#include "altimeter_renderer.h"
#include <cstdint>
namespace instrument {
struct HudInput {
  float speedKmh=86,altitudeM=275,heading=180,pitch=5,roll=0,climb=1.2f;
  uint32_t nowMs=0;
  bool attitudeValid=true;
};
class HudRenderer {
 public:
  explicit HudRenderer(bool wireOrder=false):wireOrder_(wireOrder){}
  ~HudRenderer();
  HudRenderer(const HudRenderer&)=delete;
  HudRenderer& operator=(const HudRenderer&)=delete;
  bool begin();
  // Prepare once on the producer. A slot remains immutable until DMA returns it.
  bool prepareFrame(unsigned slot,const HudInput& input);
  void renderPrepared(uint16_t* pixels,unsigned slot,AirspeedRect rect) const;
  void render(uint16_t* pixels,const HudInput& input);
  static AltimeterDamage damage();
  AltimeterDamage damagePrepared(unsigned slot) const;
  // CPU1 prepares the HUD; CPU0 submits DMA. Split raster work between them.
  static bool backgroundRegion(unsigned index){return index%3==0;}
 private:
  struct State;
  State* state_=nullptr;
  bool wireOrder_;
};
}
