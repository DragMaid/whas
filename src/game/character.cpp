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
  float diveGravity = 2.5f;  // gravity while holding down in the air
  float diveTopSpeed = 1.5f; // of maxFallSpeed
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
  if (wet > 0.0f)
    wet = std::max(0.0f, wet - dt * (flying ? FLIGHT_DRY_RATE : 1.0f));

  // Terrain grew into us: slip out to the nearest free spot, in any
  // direction, if one is close. Further than that we're walled in and have
  // to dig out (loose grains are thrown off by Unbury first).
  if (Collides(sim, pos)) {
    float best = 1e9f;
    Vector2 to = pos;
    for (int dy = -UNSTUCK_REACH; dy <= UNSTUCK_REACH; ++dy)
      for (int dx = -UNSTUCK_REACH; dx <= UNSTUCK_REACH; ++dx) {
        // Prefer up a little: standing on what buried us beats sinking
        float d = static_cast<float>(dx * dx + dy * dy) + (dy > 0 ? 0.5f : 0.0f);
        Vector2 at{std::round(pos.x) + dx, std::round(pos.y) + dy};
        if (d < best && !Collides(sim, at)) {
          best = d;
          to = at;
        }
      }
    if (best < 1e9f) {
      pos = to;
      vel = {0.0f, std::min(vel.y, 0.0f)};
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
  vel.x = dir * kTuning.walkSpeed * input.walk * speedScale + pushX;
  // The sprite turns with any sideways motion, launches and pushes included
  if (vel.x != 0.0f)
    look = vel.x > 0.0f ? 1 : -1;

  if (input.jump && grounded)
    vel.y = -kTuning.jumpSpeed * speedScale;

  // Holding down in the air dives: stronger gravity, a higher top speed
  bool diving = input.down && !grounded;
  float fall = diving ? kTuning.diveGravity : 1.0f;
  vel.y += GRAVITY * fall * sim.GetConfig().world.gravity * dt;
  vel.y = std::min(vel.y, kTuning.maxFallSpeed * (diving ? kTuning.diveTopSpeed : 1.0f) *
                              speedScale);

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
  if (grounded)
    flying = false;
}

void Character::Unbury(Simulation &sim) {
  constexpr float eps = 0.001f;
  int x0 = static_cast<int>(std::floor(pos.x + eps));
  int x1 = static_cast<int>(std::floor(pos.x + WIDTH - eps));
  int y0 = static_cast<int>(std::floor(pos.y + eps));
  int y1 = static_cast<int>(std::floor(pos.y + HEIGHT - eps));
  float cx = pos.x + WIDTH * 0.5f;
  for (int y = std::max(0, y0); y <= std::min(GRID_H - 1, y1); ++y)
    for (int x = std::max(0, x0); x <= std::min(GRID_W - 1, x1); ++x) {
      const Cell &c = sim.GetCell(x, y);
      const auto &props =
          sim.GetConfig().elements[static_cast<size_t>(c.element)];
      if (c.element == Element::AIR || !props.mobile || !props.solid)
        continue;
      // Shrugged off to the nearer side and up
      Element e = c.element;
      float side = x + 0.5f < cx ? -1.0f : 1.0f;
      float depth = static_cast<float>(y - y0) / HEIGHT;
      sim.Erase(x, y, 0);
      sim.GetParticleSystem().Spawn({x + 0.5f, y + 0.5f},
                                    {side * (18.0f + 10.0f * depth),
                                     -22.0f - 8.0f * depth},
                                    e);
    }
}

bool Character::Fits(const Simulation &sim, Vector2 pos) {
  return !Collides(sim, pos);
}

void Character::PlaceClear(const Simulation &sim) {
  for (float y = std::floor(pos.y); y >= -HEIGHT; y -= 1.0f)
    if (!Collides(sim, {pos.x, y})) {
      pos.y = y;
      break;
    }
  Step(sim, {}, 0.0f);
}

void Character::Launch(Vector2 velocity) {
  pushX += velocity.x;
  vel.y += velocity.y;
  if (velocity.y < 0.0f)
    grounded = false;
}

void Character::Ignite(int exposureTicks) {
  wet = 0.0f;
  if (burnStacks == 0)
    burnStacks = 1; // catching fire is immediate, building it up takes time
  burnExposure += exposureTicks;
  while (burnExposure >= TICKS_PER_STACK) {
    burnExposure -= TICKS_PER_STACK;
    burnStacks = std::min(burnStacks + 1, MAX_BURN_STACKS);
  }
}

void Character::Soak() {
  burnStacks = 0;
  burnExposure = 0;
  wet = WET_SECONDS;
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
  // Loose fire and water in the air count too (flying spell particles are
  // handled as hits)
  Rectangle box = Bounds();
  for (const Particle &p : sim.GetParticleSystem().Pool()) {
    if (!p.active || (p.isProjectile && p.remainingDistance > 0.0f) ||
        (p.element != Element::FIRE && p.element != Element::WATER) ||
        !CheckCollisionPointRec(p.pos, box))
      continue;
    (p.element == Element::FIRE ? inFire : inWater) = true;
  }

  if (inWater) {
    Soak();
    return;
  }
  if (inFire)
    Ignite(1);
  if (burnStacks > 0)
    hp = std::max(0.0f, hp - burnStacks * BURN_DPS * dt);
}
