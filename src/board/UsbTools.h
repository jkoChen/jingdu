#pragma once
#include "App.h"

namespace jd {
// Physical UART for native framebuffer capture and checked font import to the TF card.
class UsbTools {
  Str line, target, temporary, expectedHash;
  FILE* output = nullptr;
  uint32_t expectedBytes = 0, received = 0, lastBlock = 0;
  bool discarded = false;
  void cancel();
  void command(const Str&, App&, Device&, bool frameReady);

 public:
  bool busy() const {
    return output != nullptr;
  }
  bool poll(App&, Device&, bool frameReady);
};
}  // namespace jd
