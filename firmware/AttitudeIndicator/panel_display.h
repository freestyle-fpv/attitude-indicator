#pragma once
#include <Arduino_GFX_Library.h>
namespace instrument {
class PanelDisplay : public Arduino_CO5300 {
 public:
  using Arduino_CO5300::Arduino_CO5300;
  // Raw DMA window writes bypass the vendor cache. Force its next drawing
  // operation (status, calibration, startup error) to reprogram the window.
  void invalidateWindow(){_currentX=-1;_currentY=-1;}
};
}
