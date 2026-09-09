#ifndef INSTRUMENT_RENDERER_H
#define INSTRUMENT_RENDERER_H
#include <stdint.h>
namespace instrument {
class Renderer {
 public:
  Renderer();
  // Store RGB565 with its bytes swapped for direct SPI transfer on ESP32.
  explicit Renderer(bool wireOrder);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  bool begin();
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
 private:
  struct Data;
  Data* data_;
  bool wireOrder_;
};
}
#endif
