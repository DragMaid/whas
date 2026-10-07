#include "whas/game/match.h"
#include "whas/core/bytes.h"
#include "whas/core/det_rng.h"
#include "whas/engine/simulation.h"
#include "whas/game/arena_gen.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

namespace Match {

std::array<int, PLAYERS> TieOrder(uint64_t seed) {
  DetRng rng(seed, 0x71E);
  return rng.Below(2) ? std::array<int, PLAYERS>{1, 0}
                      : std::array<int, PLAYERS>{0, 1};
}

uint64_t RoundSeed(uint64_t matchSeed, int round) {
  return DetRng::Mix(matchSeed + 0x9E3779B97F4A7C15ull *
                                     static_cast<uint64_t>(round + 1));
}

State BeginRound(Simulation &sim, uint64_t matchSeed, int round,
                 const MatchOptions *options) {
  uint64_t seed = RoundSeed(matchSeed, round);
  const MapSpec *spec = options ? options->MapFor(round) : nullptr;
  std::array<Vector2, PLAYERS> spawns;
  if (spec && spec->custom) {
    Maps::Build(sim, *spec->custom, seed);
    spawns = spec->custom->spawns;
  } else {
    if (options)
      sim.GetConfig() = SimulationConfig{};
    sim.Restart(seed);
    spawns = ArenaGen::Generate(sim, seed).spawns;
  }

  State state;
  state.seed = matchSeed;
  state.round = round;
  state.order = TieOrder(matchSeed);
  for (int i = 0; i < PLAYERS; ++i) {
    Character &c = state.characters[i];
    c.id = i + 1; // hurtbox ids; 0 is never used
    c.pos = spawns[i];
    c.facing = c.pos.x < GRID_W * 0.5f ? 1 : -1;
    c.look = c.facing;
    c.maxHp = c.hp = MAX_HP;
    c.PlaceClear(sim); // out of the terrain, grounded before the first plan
  }
  return state;
}

namespace {

// Burn exposure (ticks) a fire projectile passing through a body leaves
constexpr int FIRE_HIT_EXPOSURE = 3;

void ApplyHits(Simulation &sim, Character *chars, int count) {
  for (const ParticleHit &hit : sim.GetParticleSystem().TakeHits()) {
    for (Character &c : std::span(chars, count)) {
      if (c.id != hit.targetId)
        continue;
      c.hp = std::max(0.0f, c.hp - hit.power * DAMAGE_PER_POWER);
      if (hit.element == Element::WATER)
        c.Soak();
      else if (hit.element == Element::FIRE)
        c.Ignite(FIRE_HIT_EXPOSURE);
    }
  }
}

// Wind fields move characters like everything else: acceleration =
// force/mass. An element's field only moves that element.
void ApplyFields(const Simulation &sim, Character *chars, int count) {
  for (const SpellEffect &effect : sim.GetActiveSpellEffects()) {
    if (effect.stats.kind != SpellKind::Field ||
        effect.stats.element != Element::AIR)
      continue;
    Vector2 push = SpellSystem::FieldPush(effect);
    for (Character &c : std::span(chars, count)) {
      if (c.id == effect.owner)
        continue;
      float strength = SpellSystem::FieldStrengthAt(effect, c.Center());
      if (strength <= 0.0f)
        continue;
      float dv = effect.stats.force * strength / Character::MASS *
                 TurnController::TICK_DT;
      c.Launch({push.x * dv, push.y * dv});
    }
  }
}

// Light bursts blind everyone close enough, the caster too: fully at the
// burst, a quarter as long at its edge, and never for long
constexpr float MAX_BLIND_SECONDS = 2.0f;
constexpr float EDGE_BLIND = 0.25f;

void ApplyFlashes(Simulation &sim, Character *chars, int count) {
  for (const Flash &flash : sim.GetParticleSystem().TakeFlashes()) {
    for (Character &c : std::span(chars, count)) {
      Vector2 d{c.Center().x - flash.pos.x, c.Center().y - flash.pos.y};
      float dist = std::sqrt(d.x * d.x + d.y * d.y);
      if (!c.Alive() || dist > flash.radius)
        continue;
      float near = 1.0f - (1.0f - EDGE_BLIND) * dist / flash.radius;
      c.flash = std::max(c.flash,
                         std::min(MAX_BLIND_SECONDS, flash.time * near));
    }
  }
}

} // namespace

void ApplyEffects(Simulation &sim, Character *characters, int count) {
  ApplyHits(sim, characters, count);
  ApplyFlashes(sim, characters, count);
  ApplyFields(sim, characters, count);
  for (Character &c : std::span(characters, count))
    c.UpdateBurn(sim, TurnController::TICK_DT);
}

void ExecuteTick(Simulation &sim, State &state,
                 const std::array<const TurnPlan *, PLAYERS> &plans,
                 int tick) {
  static const TurnPlan kEmpty;
  for (int slot : state.order)
    TurnController::ApplyPlanTick(plans[slot] ? *plans[slot] : kEmpty, tick,
                                  sim, state.characters[slot]);

  std::vector<Hurtbox> hurtboxes;
  for (const Character &c : state.characters)
    if (c.Alive())
      hurtboxes.push_back({c.id, c.Bounds()});
  sim.GetParticleSystem().SetHurtboxes(std::move(hurtboxes));
  std::vector<Cursor> cursors;
  for (const Character &c : state.characters)
    if (c.hasCursor)
      cursors.push_back({c.id, c.cursor});
  sim.GetParticleSystem().SetCursors(std::move(cursors));

  sim.Update(TurnController::TICK_DT);
  ApplyEffects(sim, state.characters.data(), PLAYERS);
}

void EndTurn(State &state) {
  for (Character &c : state.characters)
    c.CoolBurn();
}

void ExecuteTurn(Simulation &sim, State &state,
                 const std::array<const TurnPlan *, PLAYERS> &plans) {
  for (int tick = 0; tick < TurnController::TURN_TICKS; ++tick)
    ExecuteTick(sim, state, plans, tick);
  EndTurn(state);
}

int RoundWinner(const State &state) {
  bool a = state.characters[0].Alive();
  bool b = state.characters[1].Alive();
  if (a && b)
    return -1;
  if (a)
    return 0;
  if (b)
    return 1;
  return PLAYERS;
}

uint64_t Hash(const Simulation &sim, const State &state) {
  uint64_t h = sim.StateHash();
  auto mix = [&h](const void *data, size_t n) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < n; ++i) {
      h ^= bytes[i];
      h *= 0x100000001b3ull;
    }
  };
  auto addF = [&](float v) { mix(&v, sizeof v); };
  auto addI = [&](int32_t v) { mix(&v, sizeof v); };
  for (const Character &c : state.characters) {
    addF(c.pos.x);
    addF(c.pos.y);
    addF(c.vel.x);
    addF(c.vel.y);
    addF(c.pushX);
    addF(c.hp);
    addI(c.grounded);
    addI(c.facing);
    addI(c.burnStacks);
    addI(c.burnExposure);
    addF(c.wet);
  }
  return h;
}

std::string EncodeSnapshot(const Simulation &sim, const State &state) {
  ByteWriter out;
  out.Put(state.seed);
  out.Put(state.round);
  out.Put(state.order);
  for (const Character &c : state.characters) {
    out.Put(c.id);
    out.Put(c.pos);
    out.Put(c.vel);
    out.Put(c.pushX);
    out.Put(c.grounded);
    out.Put(c.facing);
    out.Put(c.hp);
    out.Put(c.maxHp);
    out.Put(c.burnStacks);
    out.Put(c.burnExposure);
    out.Put(c.wet);
  }
  std::vector<uint8_t> world = sim.SaveSnapshot();
  out.Put(static_cast<uint32_t>(world.size()));
  out.PutBytes(world.data(), world.size());
  return Base64::Encode(out.Data());
}

bool DecodeSnapshot(const std::string &text, Simulation &sim, State &state) {
  std::vector<uint8_t> bytes;
  if (!Base64::Decode(text, bytes))
    return false;
  try {
    ByteReader in(bytes);
    State next;
    next.seed = in.Get<uint64_t>();
    next.round = in.Get<int>();
    next.order = in.Get<std::array<int, PLAYERS>>();
    for (Character &c : next.characters) {
      c.id = in.Get<int>();
      c.pos = in.Get<Vector2>();
      c.vel = in.Get<Vector2>();
      c.pushX = in.Get<float>();
      c.grounded = in.Get<bool>();
      c.facing = in.Get<int>();
      c.hp = in.Get<float>();
      c.maxHp = in.Get<float>();
      c.burnStacks = in.Get<int>();
      c.burnExposure = in.Get<int>();
      c.wet = in.Get<float>();
    }
    uint32_t size = in.Get<uint32_t>();
    const uint8_t *world = in.Take(size);
    if (!in.Done() || !sim.LoadSnapshot({world, world + size}))
      return false;
    state = next;
    return true;
  } catch (const std::runtime_error &) {
    return false;
  }
}

} // namespace Match
