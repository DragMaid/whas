#include "whas/game/match.h"
#include "whas/core/bytes.h"
#include "whas/core/det_rng.h"
#include "whas/engine/simulation.h"
#include "whas/game/arena_gen.h"
#include <algorithm>
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

State BeginRound(Simulation &sim, uint64_t matchSeed, int round) {
  uint64_t seed = RoundSeed(matchSeed, round);
  sim.Reset();
  sim.SetSeed(seed);
  ArenaGen::Arena arena = ArenaGen::Generate(sim, seed);

  State state;
  state.seed = matchSeed;
  state.round = round;
  state.order = TieOrder(matchSeed);
  for (int i = 0; i < PLAYERS; ++i) {
    Character &c = state.characters[i];
    c.id = i + 1; // hurtbox ids; 0 is never used
    c.pos = arena.spawns[i];
    c.facing = c.pos.x < GRID_W * 0.5f ? 1 : -1;
    c.maxHp = c.hp = MAX_HP;
    c.Step(sim, {}, 0.0f); // resolve grounded before the first plan
  }
  return state;
}

namespace {

void ApplyHits(Simulation &sim, Character *chars, int count) {
  for (const ParticleHit &hit : sim.GetParticleSystem().TakeHits()) {
    for (Character &c : std::span(chars, count)) {
      if (c.id != hit.targetId)
        continue;
      c.hp = std::max(0.0f, c.hp - hit.power * DAMAGE_PER_POWER);
      if (hit.element == Element::WATER)
        c.burnStacks = 0;
    }
  }
}

// Gust fields push characters like everything else: acceleration = force/mass
void ApplyGusts(const Simulation &sim, Character *chars, int count) {
  for (const SpellEffect &effect : sim.GetActiveSpellEffects()) {
    if (effect.stats.kind != SpellKind::Gust)
      continue;
    for (Character &c : std::span(chars, count)) {
      if (c.id == effect.owner)
        continue;
      float strength = SpellSystem::GustStrengthAt(effect, c.Center());
      if (strength <= 0.0f)
        continue;
      float dv = effect.stats.force * strength / Character::MASS *
                 TurnController::TICK_DT;
      c.Launch({effect.direction.x * dv, effect.direction.y * dv});
    }
  }
}

} // namespace

void ApplyEffects(Simulation &sim, Character *characters, int count) {
  ApplyHits(sim, characters, count);
  ApplyGusts(sim, characters, count);
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
