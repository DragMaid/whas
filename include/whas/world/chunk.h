#pragma once
#include <cstdint>

struct Chunk {
  bool active = false;
  bool wakeNextFrame = false;
  int activeCount = 0;
  uint32_t lastChangeFrame = 0;

  void Wake() { wakeNextFrame = true; }
  void BeginFrame() {
    active = wakeNextFrame;
    wakeNextFrame = (activeCount > 0);
  }
};
