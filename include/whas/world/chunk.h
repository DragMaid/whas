#pragma once

struct Chunk {
  bool active = false;
  bool wakeNextFrame = false;
  int activeCount = 0;

  void Wake() { wakeNextFrame = true; }
  void BeginFrame() {
    active = wakeNextFrame;
    wakeNextFrame = (activeCount > 0);
  }
};
