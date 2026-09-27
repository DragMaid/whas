#pragma once
#include "whas/game/character.h"
#include "whas/game/turn_controller.h"
#include <array>
#include <cstdint>

class Simulation;

// The deterministic rules of a duel: everything two lockstep clients (and a
// replay) must compute identically. The Game adds input, drawing and pacing
// on top; tests and replay verification call this directly.
namespace Match {

constexpr int PLAYERS = 2;
constexpr int ROUNDS = 3; // best of 3
constexpr float DAMAGE_PER_POWER = 0.03f;
constexpr float MAX_HP = 100.0f;

// Which player's plan is applied first each tick, from the match seed, so
// neither slot always wins same-tick races
std::array<int, PLAYERS> TieOrder(uint64_t seed);

// Seed for one round's simulation and arena
uint64_t RoundSeed(uint64_t matchSeed, int round);

struct State {
  uint64_t seed = 0;
  int round = 0; // 0-based
  std::array<Character, PLAYERS> characters{};
  std::array<int, PLAYERS> order{0, 1};
};

// Clear the world, build the round's arena from the seed and spawn both
// players. Returns the state both clients start the round from.
State BeginRound(Simulation &sim, uint64_t matchSeed, int round);

// One tick of a turn: each plan's casts and movement (in tie order), the
// world step, then hits, gusts and burning
void ExecuteTick(Simulation &sim, State &state,
                 const std::array<const TurnPlan *, PLAYERS> &plans, int tick);

// After a world step: projectile hits, gust pushes and burning for any set of
// characters (the sandbox avatar uses this too)
void ApplyEffects(Simulation &sim, Character *characters, int count);

// After the last tick of a turn
void EndTurn(State &state);

// Run a whole turn at once (replays, tests, catching up after a rejoin)
void ExecuteTurn(Simulation &sim, State &state,
                 const std::array<const TurnPlan *, PLAYERS> &plans);

// Winner of the round so far: -1 while both stand, PLAYERS on a double KO
int RoundWinner(const State &state);

// Everything lockstep clients compare after a turn
uint64_t Hash(const Simulation &sim, const State &state);

} // namespace Match
