#include "whas/physics/movement_system.h"
#include "whas/core/config.h"
#include "whas/element/base/factory.h"
#include "whas/physics/particle_system.h"
#include <cmath>

bool MovementSystem::CanDisplace(const Cell &source, const Cell &target,
                                 const ElementContext &ctx) {
  if (source.element == target.element || (target.flags & CELL_HELD))
    return false;

  const auto &sourceProps =
      ctx.config.elements[static_cast<size_t>(source.element)];
  const auto &targetProps =
      ctx.config.elements[static_cast<size_t>(target.element)];
  
  // If target is air, always displaceable
  if (target.element == Element::AIR) return true;

  // Otherwise, if source is denser than target and target is not solid
  if (!targetProps.solid && sourceProps.density > targetProps.density) {
      return true;
  }

  return !targetProps.solid && !sourceProps.solid;
}

bool MovementSystem::TryDisplace(int x, int y, Cell &displaced, ElementContext &ctx) {
  // Prefer placing displaced cell downward first, then sides, then up.
  static constexpr int kOffsets[8][2] = {
      {0, 1},            // down
      {-1, 1}, {1, 1},   // down-left, down-right
      {-1, 0}, {1, 0},   // left, right
      {-1, -1}, {1, -1}, // up-left, up-right
      {0, -1},           // up
  };

  for (const auto &off : kOffsets) {
    int nx = x + off[0], ny = y + off[1];
    if (!ctx.grid.InBounds(nx, ny))
      continue;

    Cell &neighbor = ctx.grid.Get(nx, ny);
    // Allow displacement into a neighbor if it's AIR or if the displaced cell
    // can push/replace the neighbor according to movement rules.
    if (neighbor.element == Element::AIR || CanDisplace(displaced, neighbor, ctx)) {
      neighbor = displaced;
      neighbor.lastUpdateFrame = ctx.frameIndex;
      ctx.chunks.WakeChunkAt(nx, ny, ctx.frameIndex);
      return true;
    }
  }
  return false;
}

bool MovementSystem::TryMove(int x, int y, int tx, int ty, Cell &moved,
                             ElementContext &ctx) {
  if (!ctx.grid.InBounds(tx, ty))
    return false;
  Cell &target = ctx.grid.Get(tx, ty);

  const bool isDisplacing = (target.element != Element::AIR && CanDisplace(moved, target, ctx));

  // Relax lastUpdateFrame check if we are displacing or target is AIR.
  if (target.lastUpdateFrame == ctx.frameIndex && target.element != Element::AIR && !isDisplacing)
    return false;

  if (!CanDisplace(moved, target, ctx))
    return false;

  moved.lastUpdateFrame = ctx.frameIndex;

  if (target.element != Element::AIR) {
    bool splashedAway = false;
    const auto &movedProps = ctx.config.elements[static_cast<size_t>(moved.element)];
    
    // Splash logic: convert water to a particle instead of duplicating
    if (target.element == Element::WATER && movedProps.solid) {
      float speedSq = moved.vx * moved.vx + moved.vy * moved.vy;
      if (speedSq > 1.2f) {
        float speed = std::sqrt(speedSq);
        float intensity = (speed * moved.density) / 1000.0f;
        
        // TODO: fix this later
        // Chance to turn the water cell into a particle (1-to-1 conversion)
        float splashChance = 100;
        if (((float)(ctx.rng() % 100) / 100.0f) < splashChance) {
          Vector2 pVel = {(float)((ctx.rng() % 100) - 50) * 0.15f * intensity,
                          -speed * 0.4f - (float)(ctx.rng() % 100) * 0.1f};
          ParticleSystem::SpawnFrom(ctx, {(float)tx, (float)ty}, pVel,
                                    Element::WATER);
          splashedAway = true;
        }
      }
    }

    if (splashedAway) {
      // Water cell turned into particle, source position becomes AIR and is "open"
      ctx.grid.Get(x, y) = ElementFactory::Create(Element::AIR, ctx.config);
      ctx.grid.Get(x, y).lastUpdateFrame = ctx.frameIndex - 1; 
    } else {
      Cell displaced = target;
      displaced.lastUpdateFrame = ctx.frameIndex;

      if (!TryDisplace(tx, ty, displaced, ctx)) {
        // Fallback: Swap with source position. 
        // Set to frameIndex - 1 to allow fluid to fill in more aggressively if needed,
        // though usually swapped elements should stay for one frame.
        // We use frameIndex - 1 here to resolve the "unable to fill hole" issue for fluids.
        ctx.grid.Get(x, y) = displaced;
        ctx.grid.Get(x, y).lastUpdateFrame = ctx.frameIndex - 1;
      } else {
        // Pushed to neighbor, vacate source and leave it "open" for other elements
        ctx.grid.Get(x, y) = ElementFactory::Create(Element::AIR, ctx.config);
        ctx.grid.Get(x, y).lastUpdateFrame = ctx.frameIndex - 1;
      }
    }
  } else {
    // Vacate source position and leave it "open" for the same frame
    ctx.grid.Get(x, y) = ElementFactory::Create(Element::AIR, ctx.config);
    ctx.grid.Get(x, y).lastUpdateFrame = ctx.frameIndex - 1;
  }

  target = moved;
  ctx.chunks.WakeChunkAt(tx, ty, ctx.frameIndex);
  ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex);
  return true;
}

void MovementSystem::SetNext(int x, int y, const Cell &c, ElementContext &ctx) {
  Cell &destination = ctx.grid.Get(x, y);
  destination = c;
  destination.lastUpdateFrame = ctx.frameIndex;
  ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex);
}

void MovementSystem::Carry(int x, int y, ElementContext &ctx) {
  Cell &c = ctx.grid.Get(x, y);
  c.lastUpdateFrame = ctx.frameIndex;
}
