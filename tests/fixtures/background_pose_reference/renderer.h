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
 private:
  struct Data;
  Data* data_;
  bool wireOrder_;
};
}
#endif
