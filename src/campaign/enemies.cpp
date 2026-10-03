#include "whas/campaign/enemies.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/character_draw.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/spell_quant.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace Enemies {

namespace {

struct Tuning {
  float sight = 150.0f;      // cells a mage casts across
  float keepAway = 45.0f;    // mages back off closer than this
  float approach = 80.0f;    // ...and close in further than this
  float touchCooldown = 0.8f;
  Vector2 knockback{45.0f, -30.0f};
  float repathEvery = 0.4f;
  float flyerTurn = 6.0f; // how quickly a flyer's velocity follows its want
  int node = 4;           // cells per path node
  int maxExpansions = 5000;
};
constexpr Tuning kTuning;

float Len(Vector2 v) { return std::hypot(v.x, v.y); }
Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }

bool Solid(const Simulation &sim, int x, int y) {
  if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
    return true;
  const Cell &c = sim.GetCell(x, y);
  if (c.element == Element::AIR)
    return false;
  const auto &props = sim.GetConfig().elements[static_cast<size_t>(c.element)];
  return props.solid && !props.passable;
}

// Whether a body centred on a can slide straight to b
bool BodyClear(const Simulation &sim, Vector2 a, Vector2 b) {
  Vector2 d = Sub(b, a);
  int steps = std::max(1, static_cast<int>(std::ceil(Len(d))));
  for (int i = 0; i <= steps; ++i) {
    float t = static_cast<float>(i) / steps;
    Vector2 c{a.x + d.x * t, a.y + d.y * t};
    if (!Character::Fits(sim, {c.x - Character::WIDTH * 0.5f,
                               c.y - Character::HEIGHT * 0.5f}))
      return false;
  }
  return true;
}

// An enemy's speed (cells/s) as a share of the player's walk
float WalkScale(float speed) { return std::clamp(speed / 30.0f, 0.1f, 3.0f); }

// Walkers: a step's worth of keys toward (dir) with a jump when the way is
// walled or the target is above
CharacterInput Walk(const Simulation &sim, const Character &b, int dir,
                    bool wantUp) {
  CharacterInput in;
  in.left = dir < 0;
  in.right = dir > 0;
  if (b.grounded && dir != 0) {
    Vector2 ahead{b.pos.x + dir * 2.0f, b.pos.y - 1.0f};
    if (!Character::Fits(sim, ahead))
      in.jump = true;
  }
  if (b.grounded && wantUp)
    in.jump = true;
  return in;
}

void Touch(Enemy &e, Character &player) {
  if (e.touchIn > 0.0f || !player.Alive() ||
      !CheckCollisionRecs(e.body.Bounds(), player.Bounds()))
    return;
  float side = player.Center().x >= e.body.Center().x ? 1.0f : -1.0f;
  player.hp = std::max(0.0f, player.hp - e.def.damage);
  player.Launch({side * kTuning.knockback.x, kTuning.knockback.y});
  e.touchIn = kTuning.touchCooldown;
}

void TickMage(Enemy &e, Simulation &sim, const Character &player,
              std::mt19937 &rng, float dt) {
  Character &b = e.body;
  Vector2 to = Sub(player.Center(), b.Center());
  float dist = Len(to);
  bool sees = dist < kTuning.sight && LineOfSight(sim, b.Center(), player.Center());

  int dir = 0;
  if (sees && dist > kTuning.approach)
    dir = to.x > 0 ? 1 : -1;
  else if (sees && dist < kTuning.keepAway)
    dir = to.x > 0 ? -1 : 1;
  else if (!sees)
    dir = e.wander;
  if (dir != 0 && b.grounded && !Character::Fits(sim, {b.pos.x + dir * 2.0f, b.pos.y - 8.0f}))
    e.wander = -e.wander; // a wall too tall to hop
  CharacterInput in = Walk(sim, b, dir, false);
  in.walk = WalkScale(e.def.speed);
  b.Step(sim, in, dt);

  e.castIn -= dt;
  if (!sees || e.castIn > 0.0f || e.spells.empty())
    return;
  const SpellStats &s =
      e.spells[std::uniform_int_distribution<size_t>(0, e.spells.size() - 1)(rng)];
  if (!b.CanCast(s.HasFlight()))
    return;
  // Lead the target a little: where it will be when the spell gets there
  float t = s.speed > 0.0f ? dist / s.speed : 0.0f;
  Vector2 aim{to.x + player.vel.x * t * 0.5f, to.y + player.vel.y * t * 0.5f};
  float len = Len(aim);
  if (len < 0.01f)
    return;
  aim = SpellQuant::SnapAim({aim.x / len, aim.y / len});
  sim.CastSpell(s, b.Center(), aim, b.id);
  if (s.HasFlight())
    b.LaunchFlight(SpellSystem::FlightVelocity(s, aim));
  std::uniform_real_distribution<float> jitter(0.8f, 1.25f);
  e.castIn = e.def.castEvery * jitter(rng) +
             TurnController::CastTicks(s) * TurnController::TICK_DT;
}

void TickUndead(Enemy &e, Simulation &sim, const Character &player, float dt) {
  Character &b = e.body;
  Vector2 to = Sub(player.Center(), b.Center());
  bool chasing = player.Alive() && Len(to) < 220.0f;
  int dir = chasing ? (std::abs(to.x) > 1.5f ? (to.x > 0 ? 1 : -1) : 0)
                    : e.wander;
  if (!chasing && b.grounded &&
      !Character::Fits(sim, {b.pos.x + dir * 2.0f, b.pos.y - 8.0f}))
    e.wander = -e.wander;
  bool climb = chasing && to.y < -Character::HEIGHT && std::abs(to.x) < 24.0f;
  CharacterInput in = Walk(sim, b, dir, climb);
  in.walk = WalkScale(e.def.speed) * (chasing ? 1.0f : 0.5f);
  b.Step(sim, in, dt);
}

// Move a flyer by its velocity, sliding along whatever it bumps
void Fly(Character &b, const Simulation &sim, float dt) {
  Vector2 d{b.vel.x * dt, b.vel.y * dt};
  int steps = std::max(1, static_cast<int>(std::ceil(Len(d) / 0.5f)));
  for (int i = 0; i < steps; ++i) {
    Vector2 nx{b.pos.x + d.x / steps, b.pos.y};
    if (Character::Fits(sim, nx))
      b.pos = nx;
    else
      b.vel.x = 0.0f;
    Vector2 ny{b.pos.x, b.pos.y + d.y / steps};
    if (Character::Fits(sim, ny))
      b.pos = ny;
    else
      b.vel.y = 0.0f;
  }
  if (b.vel.x != 0.0f)
    b.look = b.vel.x > 0.0f ? 1 : -1;
  b.grounded = false;
}

void TickFlyer(Enemy &e, Simulation &sim, const Character &player,
               std::mt19937 &rng, float dt) {
  Character &b = e.body;
  if (b.wet > 0.0f)
    b.wet = std::max(0.0f, b.wet - dt);
  // Knockback and gusts arrive as pushes; flyers take them as velocity
  b.vel.x += b.pushX;
  b.pushX = 0.0f;

  Vector2 target = player.Center();
  bool direct = BodyClear(sim, b.Center(), target);
  e.repathIn -= dt;
  if (!direct && (e.repathIn <= 0.0f || e.path.empty())) {
    e.path = FindPath(sim, b.Center(), target);
    e.repathIn = kTuning.repathEvery;
  }
  if (direct)
    e.path.clear();
  else if (!e.path.empty()) {
    target = e.path.front();
    if (Len(Sub(target, b.Center())) < 2.5f) {
      e.path.erase(e.path.begin());
      if (!e.path.empty())
        target = e.path.front();
    }
  } else {
    // Nowhere to go: hover where it is
    target = b.Center();
  }

  Vector2 to = Sub(target, b.Center());
  float len = Len(to);
  Vector2 want = len > 0.5f ? Vector2{to.x / len * e.def.speed, to.y / len * e.def.speed}
                            : Vector2{0, 0};
  float k = std::min(1.0f, kTuning.flyerTurn * dt);
  b.vel.x += (want.x - b.vel.x) * k;
  b.vel.y += (want.y - b.vel.y) * k;
  Fly(b, sim, dt);

  // Not getting anywhere: look again, with a nudge to shake loose
  if (Len(Sub(b.pos, e.lastPos)) < 0.5f * dt * e.def.speed && len > 3.0f) {
    e.stuckFor += dt;
    if (e.stuckFor > 0.6f) {
      std::uniform_real_distribution<float> nudge(-1.0f, 1.0f);
      b.vel = {nudge(rng) * e.def.speed, nudge(rng) * e.def.speed};
      e.repathIn = 0.0f;
      e.path.clear();
      e.stuckFor = 0.0f;
    }
  } else {
    e.stuckFor = 0.0f;
  }
  e.lastPos = b.pos;
}

} // namespace

void Spawn(const Campaign::RoomDef &room, const Simulation &sim,
           std::vector<Enemy> &out) {
  out.clear();
  int id = FIRST_ID;
  for (const Campaign::EnemyDef &def : room.enemies) {
    Enemy e;
    e.def = def;
    e.body.id = id++;
    e.body.pos = def.pos;
    e.body.maxHp = e.body.hp = def.hp;
    e.lastPos = def.pos;
    if (def.kind == Campaign::EnemyKind::Flyer) {
      if (!Character::Fits(sim, e.body.pos))
        e.body.PlaceClear(sim);
    } else {
      e.body.PlaceClear(sim);
    }
    for (const Spell &spell : def.spells) {
      SpellStats s = SpellQuant::Canonical(spell);
      if (s.valid)
        e.spells.push_back(std::move(s));
    }
    // Don't all fire on the first frame
    e.castIn = 0.8f + 0.25f * static_cast<float>(out.size() % 4);
    out.push_back(std::move(e));
  }
}

void Tick(Enemy &e, Simulation &sim, Character &player, std::mt19937 &rng,
          float dt) {
  if (!e.body.Alive())
    return;
  e.touchIn = std::max(0.0f, e.touchIn - dt);
  e.body.Unbury(sim);
  switch (e.def.kind) {
  case Campaign::EnemyKind::Mage:
    TickMage(e, sim, player, rng, dt);
    break;
  case Campaign::EnemyKind::Undead:
    TickUndead(e, sim, player, dt);
    break;
  case Campaign::EnemyKind::Flyer:
    TickFlyer(e, sim, player, rng, dt);
    break;
  }
  Touch(e, player);
}

bool LineOfSight(const Simulation &sim, Vector2 a, Vector2 b) {
  Vector2 d = Sub(b, a);
  int steps = static_cast<int>(std::ceil(Len(d)));
  for (int i = 1; i < steps; ++i) {
    float t = static_cast<float>(i) / steps;
    if (Solid(sim, static_cast<int>(std::floor(a.x + d.x * t)),
              static_cast<int>(std::floor(a.y + d.y * t))))
      return false;
  }
  return true;
}

std::vector<Vector2> FindPath(const Simulation &sim, Vector2 from, Vector2 to) {
  const int N = kTuning.node;
  const int cols = GRID_W / N, rows = GRID_H / N;
  std::vector<int8_t> open(static_cast<size_t>(cols) * rows, -1); // -1 unknown
  auto centre = [N](int i, int j) {
    return Vector2{i * N + N * 0.5f, j * N + N * 0.5f};
  };
  auto passable = [&](int i, int j) {
    if (i < 0 || j < 0 || i >= cols || j >= rows)
      return false;
    int8_t &o = open[static_cast<size_t>(j) * cols + i];
    if (o < 0) {
      Vector2 c = centre(i, j);
      o = Character::Fits(sim, {c.x - Character::WIDTH * 0.5f,
                                c.y - Character::HEIGHT * 0.5f});
    }
    return o == 1;
  };
  // Nearest open node to a point, looking a few nodes around it
  auto nearest = [&](Vector2 p, int &oi, int &oj) {
    int pi = static_cast<int>(p.x) / N, pj = static_cast<int>(p.y) / N;
    for (int r = 0; r <= 4; ++r)
      for (int dj = -r; dj <= r; ++dj)
        for (int di = -r; di <= r; ++di)
          if (std::max(std::abs(di), std::abs(dj)) == r &&
              passable(pi + di, pj + dj)) {
            oi = pi + di, oj = pj + dj;
            return true;
          }
    return false;
  };
  int si, sj, gi, gj;
  if (!nearest(from, si, sj) || !nearest(to, gi, gj))
    return {};

  auto index = [cols](int i, int j) { return j * cols + i; };
  std::vector<float> cost(open.size(), 1e30f);
  std::vector<int> came(open.size(), -1);
  using Item = std::pair<float, int>;
  std::priority_queue<Item, std::vector<Item>, std::greater<>> frontier;
  auto h = [&](int i, int j) {
    float dx = std::abs(i - gi), dy = std::abs(j - gj);
    return std::max(dx, dy) + 0.414f * std::min(dx, dy);
  };
  cost[index(si, sj)] = 0.0f;
  frontier.push({h(si, sj), index(si, sj)});
  int goal = index(gi, gj), expansions = 0;
  while (!frontier.empty() && expansions++ < kTuning.maxExpansions) {
    auto [f, cur] = frontier.top();
    frontier.pop();
    if (cur == goal)
      break;
    int ci = cur % cols, cj = cur / cols;
    for (int dj = -1; dj <= 1; ++dj)
      for (int di = -1; di <= 1; ++di) {
        if (!di && !dj)
          continue;
        int ni = ci + di, nj = cj + dj;
        if (!passable(ni, nj))
          continue;
        // No cutting corners through terrain
        if (di && dj && (!passable(ci + di, cj) || !passable(ci, cj + dj)))
          continue;
        float c = cost[cur] + (di && dj ? 1.414f : 1.0f);
        int n = index(ni, nj);
        if (c < cost[n]) {
          cost[n] = c;
          came[n] = cur;
          frontier.push({c + h(ni, nj), n});
        }
      }
  }
  if (came[goal] < 0 && goal != index(si, sj))
    return {};
  std::vector<Vector2> path;
  for (int n = goal; n != index(si, sj); n = came[n])
    path.push_back(centre(n % cols, n / cols));
  std::reverse(path.begin(), path.end());
  // Skip waypoints already in a straight clear line, so flight is smooth
  std::vector<Vector2> smooth;
  Vector2 at = from;
  for (size_t i = 0; i < path.size(); ++i) {
    bool last = i + 1 == path.size();
    if (last || !BodyClear(sim, at, path[i + 1])) {
      smooth.push_back(path[i]);
      at = path[i];
    }
  }
  return smooth;
}

void Draw(const Enemy &e) {
  if (!e.body.Alive())
    return;
  // TODO: real sprites per kind; tinted markers stand in for now
  Color tint;
  switch (e.def.kind) {
  case Campaign::EnemyKind::Mage:
    tint = {170, 110, 230, 255};
    break;
  case Campaign::EnemyKind::Undead:
    tint = {120, 190, 110, 255};
    break;
  case Campaign::EnemyKind::Flyer:
  default:
    tint = {230, 170, 80, 255};
    break;
  }
  DrawCharacterBody(e.body, tint, true);
  Rectangle r{e.body.pos.x * CELL_SIZE, e.body.pos.y * CELL_SIZE,
              Character::WIDTH * CELL_SIZE, Character::HEIGHT * CELL_SIZE};
  float cx = r.x + r.width * 0.5f;
  if (e.def.kind == Campaign::EnemyKind::Mage) {
    // A pointed hat
    DrawTriangle({cx, r.y - 22}, {cx - 12, r.y - 4}, {cx + 12, r.y - 4}, tint);
  } else if (e.def.kind == Campaign::EnemyKind::Flyer) {
    float flap = std::sin(static_cast<float>(GetTime()) * 18.0f) * 6.0f;
    DrawLineEx({cx, r.y + 14}, {cx - 22, r.y + 4 + flap}, 3.0f, tint);
    DrawLineEx({cx, r.y + 14}, {cx + 22, r.y + 4 + flap}, 3.0f, tint);
  } else {
    // Hollow green eyes
    DrawCircle(static_cast<int>(cx - 5), static_cast<int>(r.y + 12), 2.5f, {180, 255, 140, 255});
    DrawCircle(static_cast<int>(cx + 5), static_cast<int>(r.y + 12), 2.5f, {180, 255, 140, 255});
  }
}

} // namespace Enemies
