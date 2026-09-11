#pragma once
#include <cstdint>

namespace instrument {
struct AirspeedRect { int x,y,width,height; };
class AirspeedRenderer {
 public:
  explicit AirspeedRenderer(bool wireOrder=false);
  ~AirspeedRenderer();
  AirspeedRenderer(const AirspeedRenderer&)=delete;
  AirspeedRenderer& operator=(const AirspeedRenderer&)=delete;
  bool begin();
  // Initialize once before rendering. Subsequent drawing reads immutable caches;
  // disjoint destinations can be rendered concurrently. No per-frame allocations.
  // Band origin/stride matches circleBandBounds when compactCircle is enabled.
  // Invalid ranges do not write. Nonfinite speeds draw the face without a hand.
  void render(uint16_t* pixels,float speedKmh);
  void renderBackgroundBand(uint16_t* pixels,int firstRow,int rows,bool compactCircle=false) const;
  void renderForegroundBand(uint16_t* pixels,float speedKmh,int firstRow,int rows,bool compactCircle=false) const;
  // Packed rectangular updates restore the old hand from the immutable face.
  static AirspeedRect damage(float previousKmh,float nextKmh);
  void renderRectangle(uint16_t* pixels,float speedKmh,AirspeedRect rect) const;
  void renderBackgroundRectangle(uint16_t* pixels,AirspeedRect rect) const;
  void renderForegroundRectangle(uint16_t* pixels,float speedKmh,AirspeedRect rect) const;
 private:
  void foreground(uint16_t* pixels,float speedKmh,AirspeedRect rect) const;
  struct Data;
  Data* data_=nullptr;
  bool wireOrder_;
};
}
