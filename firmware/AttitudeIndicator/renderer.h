#ifndef INSTRUMENT_RENDERER_H
#define INSTRUMENT_RENDERER_H
#include <stdint.h>
#include "installation.h"
namespace instrument {
class RoundDamage {
 public:
  RoundDamage()=default;
  RoundDamage(float previousPitch,float previousRoll,float pitch,float roll,UsbSide side,bool fpsChanged=false);
  AirspeedRect window(AirspeedRect band)const;
  // The renderer's drawing primitives also emit their old and new footprints.
  void line(float,float,float,float,float,uint16_t);
  void triangle(float,float,float,float,float,float,uint16_t);
  void pitchDigit(int,float,float,float,float);
  float limit=0;
 private:
  void add(float left,float top,float right,float bottom,float padding);
  struct Horizon {float sine,cosine,pitchSine,pitchCosine;};
  Horizon horizons_[2]={};
  static constexpr int MarkRows=4;
  static constexpr int MarkColumns=16;
  uint32_t marks_[(ScreenSize+MarkRows-1)/MarkRows]={};
  bool valid_=false;
};
class Renderer {
 public:
  Renderer();
  // Store RGB565 with its bytes swapped for direct SPI transfer on ESP32.
  explicit Renderer(bool wireOrder);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  bool begin();
  // Call between frames, when neither core is using the renderer.
  bool setInstallation(UsbSide side);
  static constexpr int StableBottomRow=400;
  static bool hasStableBottom(float pitchDegrees,float rollDegrees);
  void render(uint16_t* rgb565, float pitchDegrees, float rollDegrees);
  // Destination row zero represents firstRow; storage holds rowCount*466 pixels.
  // Invalid row ranges and null destinations are ignored.
  void renderBand(uint16_t* rgb565, float pitchDegrees, float rollDegrees, int firstRow, int rowCount);
  // Render an ordered background/foreground pair into the same destination,
  // using exactly the same pose, row range and compactCircle flag for both calls.
  // By default storage holds rowCount*466 pixels. With compactCircle, storage
  // holds rowCount*circleBandBounds(firstRow,rowCount).width pixels, with column
  // zero representing that band's x coordinate. Invalid ranges
  // and null destinations are ignored. Missing data or nonfinite poses make
  // background fill black and foreground do nothing.
  // After begin(), background only reads immutable data and may run alongside
  // foreground on disjoint buffers. Serialize all foreground/renderBand/render
  // calls on one owner; begin() and destruction must not overlap rendering.
  void renderBackgroundBand(uint16_t* rgb565, float pitchDegrees, float rollDegrees, int firstRow, int rowCount, bool compactCircle=false);
  void renderForegroundBand(uint16_t* rgb565, float pitchDegrees, float rollDegrees, int firstRow, int rowCount, bool compactCircle=false);
  void renderBackgroundWindow(uint16_t* rgb565,float pitchDegrees,float rollDegrees,AirspeedRect rect);
  void renderForegroundWindow(uint16_t* rgb565,float pitchDegrees,float rollDegrees,AirspeedRect rect);
  // Prepare one immutable pose before drawing disjoint windows on both cores.
  void prepareForeground(float pitch,float roll);
  void renderPreparedForegroundWindow(uint16_t* rgb565,AirspeedRect rect,bool timings=false) const;
 private:
  struct Data;
  bool begin(UsbSide side);
  static void release(Data* data);
  Data* data_;
  bool wireOrder_;
};
}
#endif
