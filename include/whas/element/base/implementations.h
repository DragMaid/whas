#pragma once
#include "whas/element/base/econtext.h"

namespace ElementsImpl
{
    void UpdateWater(int x, int y, ElementContext& ctx);
    void UpdateEarth(int x, int y, ElementContext& ctx);
    void UpdateFire (int x, int y, ElementContext& ctx);
    void UpdateSteam(int x, int y, ElementContext& ctx);
    void UpdateCloud(int x, int y, ElementContext& ctx);
    void UpdateIce  (int x, int y, ElementContext& ctx);
    void UpdateSand (int x, int y, ElementContext& ctx);
}
