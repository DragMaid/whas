#include "whas/game/character.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include <algorithm>
#include <cmath>

namespace {

struct CharacterTuning {
  float walkSpeed = 30.0f; // cells/s
  float jumpSpeed = 38.0f; // cells/s upwards
  float groundFriction = 8.0f; // push velocity lost per second, grounded
  float airDrag = 0.6f;        // push velocity lost per second, airborne
  float maxFallSpeed = 70.0f;
  float liquidSlowdown = 0.5f;
  float maxSubstep = 0.5f; // cells per collision substep
};

constexpr CharacterTuning kTuning;

// Characters stand on solids and wade through liquids and gases
bool IsBlocking(const Simulation &sim, int x, int y) {
  if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
    return true;
  const Cell &c = sim.GetCell(x, y);
  if (c.element == Element::AIR)
    return false;
  const auto &props =
      sim.GetConfig().elements[static_cast<size_t>(c.element)];
  bool liquid = props.mobile && !props.solid;
  return !props.passable && !liquid;
}

bool IsLiquid(const Simulation &sim, int x, int y) {
  if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
    return false;
  const auto &props = sim.GetConfig()
                          .elements[static_cast<size_t>(sim.GetCell(x, y).element)];
  return props.mobile && !props.solid;
}

bool Collides(const Simulation &sim, Vector2 pos) {
  // Shrink slightly so a box resting exactly on a cell edge isn't overlapping
  constexpr float eps = 0.001f;
  int x0 = static_cast<int>(std::floor(pos.x + eps));
  int x1 = static_cast<int>(std::floor(pos.x + Character::WIDTH - eps));
  int y0 = static_cast<int>(std::floor(pos.y + eps));
  int y1 = static_cast<int>(std::floor(pos.y + Character::HEIGHT - eps));
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
      if (IsBlocking(sim, x, y))
        return true;
  return false;
}

} // namespace

void Character::Step(const Simulation &sim, CharacterInput input, float dt) {
  // Buried (a spell dropped sand or earth on us, or made it under our feet):
  // pop up onto the top of the pile instead of being stuck inside it
  if (Collides(sim, pos)) {
    for (float y = std::floor(pos.y) - 1.0f; y >= -HEIGHT; y -= 1.0f) {
      if (!Collides(sim, {pos.x, y})) {
        pos.y = y;
        vel.y = std::min(vel.y, 0.0f);
        grounded = true;
        break;
      }
    }
  }

  Vector2 center = Center();
  bool inLiquid = IsLiquid(sim, static_cast<int>(center.x),
                           static_cast<int>(center.y));
  float speedScale = inLiquid ? kTuning.liquidSlowdown : 1.0f;

  int dir = (input.right ? 1 : 0) - (input.left ? 1 : 0);
  if (dir != 0)
    facing = dir;
  float drag = grounded ? kTuning.groundFriction : kTuning.airDrag;
  pushX *= std::max(0.0f, 1.0f - drag * dt);
  if (std::abs(pushX) < 0.1f)
    pushX = 0.0f;
  vel.x = dir * kTuning.walkSpeed * speedScale + pushX;

  if (input.jump && grounded)
    vel.y = -kTuning.jumpSpeed * speedScale;

  vel.y += GRAVITY * sim.GetConfig().world.gravity * dt;
  vel.y = std::min(vel.y, kTuning.maxFallSpeed * speedScale);

  // Horizontal: substep, stepping up one-cell ledges so sand bumps are walkable
  float dx = vel.x * dt;
  int stepsX = std::max(1, (int)std::ceil(std::abs(dx) / kTuning.maxSubstep));
  for (int i = 0; i < stepsX; ++i) {
    Vector2 next{pos.x + dx / stepsX, pos.y};
    if (!Collides(sim, next)) {
      pos = next;
      continue;
    }
    Vector2 stepUp{next.x, next.y - 1.0f};
    if (grounded && !Collides(sim, stepUp)) {
      pos = stepUp;
      continue;
    }
    vel.x = 0.0f;
    pushX = 0.0f; // hit a wall
    break;
  }

  // Vertical
  float dy = vel.y * dt;
  int stepsY = std::max(1, (int)std::ceil(std::abs(dy) / kTuning.maxSubstep));
  for (int i = 0; i < stepsY; ++i) {
    Vector2 next{pos.x, pos.y + dy / stepsY};
    if (!Collides(sim, next)) {
      pos = next;
      continue;
    }
    if (dy > 0.0f) {
      // Settle flush onto the surface instead of hovering a substep above it
      Vector2 flush{pos.x, std::floor(next.y + HEIGHT) - HEIGHT};
      if (!Collides(sim, flush))
        pos = flush;
    }
    vel.y = 0.0f;
    break;
  }

  grounded = Collides(sim, {pos.x, pos.y + 0.05f});
}

void Character::Launch(Vector2 velocity) {
  pushX += velocity.x;
  vel.y += velocity.y;
  if (velocity.y < 0.0f)
    grounded = false;
}

void Character::UpdateBurn(const Simulation &sim, float dt) {
  if (!Alive())
    return;
  bool inFire = false;
  bool inWater = false;
  int x0 = std::max(0, static_cast<int>(std::floor(pos.x)));
  int x1 = std::min(GRID_W - 1, static_cast<int>(std::floor(pos.x + WIDTH)));
  int y0 = std::max(0, static_cast<int>(std::floor(pos.y)));
  int y1 = std::min(GRID_H - 1, static_cast<int>(std::floor(pos.y + HEIGHT)));
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const Cell &c = sim.GetCell(x, y);
      if (c.element == Element::FIRE || (c.flags & CELL_BURNING))
        inFire = true;
      else if (c.element == Element::WATER)
        inWater = true;
    }
  }

  if (inWater) {
    burnStacks = 0;
    burnExposure = 0;
    return;
  }
  if (inFire && ++burnExposure >= TICKS_PER_STACK) {
    burnExposure = 0;
    burnStacks = std::min(burnStacks + 1, MAX_BURN_STACKS);
  } else if (inFire && burnStacks == 0) {
    burnStacks = 1; // catching fire is immediate, building it up takes time
  }
  if (burnStacks > 0)
    hp = std::max(0.0f, hp - burnStacks * BURN_DPS * dt);
}
