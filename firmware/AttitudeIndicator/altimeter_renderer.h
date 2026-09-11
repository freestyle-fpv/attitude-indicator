#pragma once
#include "airspeed_renderer.h"
namespace instrument {
struct AltimeterDamage {
  static constexpr int Capacity=40;
  AirspeedRect regions[Capacity]={};
  int count=0;
};
class AltimeterRenderer {
 public:
  explicit AltimeterRenderer(bool wireOrder=false):wireOrder_(wireOrder){}
  ~AltimeterRenderer();
  AltimeterRenderer(const AltimeterRenderer&)=delete;
  AltimeterRenderer& operator=(const AltimeterRenderer&)=delete;
  bool begin();
  static constexpr unsigned FrameSlots=3;
  // A slot is prepared by its owner, then disjoint regions may be drawn on both
  // cores. Do not prepare/reuse it until both drawing and DMA have completed.
  bool prepareFrame(unsigned slot,float altitudeM) const;
  void renderForegroundPrepared(uint16_t*,unsigned slot,AirspeedRect) const;
  // Convenience single-frame calls use slot 0 and are not concurrent with prepare.
  // All values are meters. Invalid values omit the hands; finite values clamp to 0-3000.
  void render(uint16_t* pixels,float altitudeM) const;
  void renderRectangle(uint16_t*,float altitudeM,AirspeedRect) const;
  void renderBackgroundRectangle(uint16_t*,AirspeedRect) const;
  void renderForegroundRectangle(uint16_t*,float altitudeM,AirspeedRect) const;
  // Non-overlapping, even-aligned horizontal regions in scan order. Their total
  // area never exceeds one frame, so existing PSRAM slots can store them packed.
  static AltimeterDamage damage(float previousM,float nextM);
 private:
  struct Data;
  Data* data_=nullptr;
  bool wireOrder_;
};
}
