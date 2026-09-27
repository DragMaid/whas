#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

namespace {

constexpr int DX4[] = {0, 0, -1, 1};
constexpr int DY4[] = {-1, 1, 0, 0};

bool TouchesWater(int x, int y, const ElementContext &ctx) {
  for (int i = 0; i < 4; ++i) {
    int nx = x + DX4[i], ny = y + DY4[i];
    if (ctx.grid.InBounds(nx, ny) &&
        ctx.grid.Get(nx, ny).element == Element::WATER)
      return true;
  }
  return false;
}

// Put a new cell into the air at (x, y) if there is room
void EmitInto(int x, int y, Element element, float temperature,
              ElementContext &ctx) {
  if (!ctx.grid.InBounds(x, y) || ctx.grid.Get(x, y).element != Element::AIR)
    return;
  Cell c = ElementFactory::Create(element, ctx.config);
  if (temperature > 0.0f)
    c.temperature = temperature;
  MovementSystem::SetNext(x, y, c, ctx);
}

void Scorch(int x, int y, ElementContext &ctx) {
  for (int i = 0; i < 4; ++i) {
    int nx = x + DX4[i], ny = y + DY4[i];
    if (!ctx.grid.InBounds(nx, ny))
      continue;
    Cell &n = ctx.grid.Get(nx, ny);
    if (n.element == Element::EARTH || n.element == Element::ROCK ||
        n.element == Element::SAND || n.element == Element::WOOD)
      n.flags |= CELL_CHARRED;
  }
}

// Shared by every flammable element: catch fire when hot enough, burn
// through the fuel while shedding flames and smoke, then burn away
void UpdateFlammable(int x, int y, ElementContext &ctx, Element burnsInto) {
  const auto &fire = ctx.config.fire;
  const auto &props = ctx.config.elements[static_cast<size_t>(
      ctx.grid.Get(x, y).element)];
  Cell src = ctx.grid.Get(x, y);

  if (!(src.flags & CELL_BURNING)) {
    if (src.temperature >= props.ignitionTemp &&
        ctx.rng.Unit() < props.flammability && !TouchesWater(x, y, ctx)) {
      src.flags |= CELL_BURNING;
      src.lifetime = props.burnFuel;
    }
    MovementSystem::SetNext(x, y, src, ctx);
    return;
  }

  if (TouchesWater(x, y, ctx)) {
    src.flags &= ~CELL_BURNING;
    src.flags |= CELL_CHARRED;
    src.temperature = std::min(src.temperature, fire.extinguishTemp);
    MovementSystem::SetNext(x, y, src, ctx);
    return;
  }

  src.lifetime -= 1.0f / 60.0f;
  src.temperature = std::max(src.temperature, props.burnTemp);
  src.flags |= CELL_CHARRED;

  // Pass the fire along: touching flammables heat up like under a flame
  for (int i = 0; i < 4; ++i) {
    int nx = x + DX4[i], ny = y + DY4[i];
    if (!ctx.grid.InBounds(nx, ny))
      continue;
    Cell &n = ctx.grid.Get(nx, ny);
    if (ctx.config.elements[static_cast<size_t>(n.element)].flammability > 0)
      n.temperature += fire.contactHeat;
  }

  if (src.lifetime <= 0.0f) {
    Scorch(x, y, ctx);
    Cell after = ElementFactory::Create(burnsInto, ctx.config);
    after.temperature = src.temperature * 0.5f;
    MovementSystem::SetNext(x, y, after, ctx);
    // Burnt-out terrain changes the static collision mesh
    ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex, props.staticTerrain);
    return;
  }

  // Flames lick upward (or sideways when something sits on top)
  if (ctx.rng.Unit() < fire.flameChance) {
    int dx = static_cast<int>(ctx.rng.Below(3)) - 1;
    EmitInto(x + dx, y - 1, Element::FIRE, props.burnTemp, ctx);
  }
  if (ctx.rng.Unit() < fire.smokeChance)
    EmitInto(x, y - 1, Element::SMOKE, 0.0f, ctx);

  MovementSystem::SetNext(x, y, src, ctx);
}

} // namespace

void UpdateWood(int x, int y, ElementContext &ctx) {
  // Burnt wood leaves smoke rather than an empty hole
  UpdateFlammable(x, y, ctx, Element::SMOKE);
}

void UpdateGrass(int x, int y, ElementContext &ctx) {
  UpdateFlammable(x, y, ctx, Element::AIR);
}

} // namespace ElementsImpl
