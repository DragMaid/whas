#pragma once

struct LiquidProperties {
  float density;
  float viscosity; // 0.0 (very fluid) to 1.0 (thick)
  float maxFallSpeed;
  float maxHorizontalSpeed;
  float spreadFactor;
  float friction;
  bool canDisplaceGas;
  bool canErodeTerrain;
};
