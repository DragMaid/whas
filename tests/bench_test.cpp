#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/match.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>

// Frame cost of the simulation on a busy arena:  whas_tests "[.bench]"
TEST_CASE("simulation step cost", "[.bench]") {
  Simulation sim;
  Match::BeginRound(sim, 12345, 0);
  sim.Paint(160, 60, Element::WATER, 20);
  sim.Paint(100, 60, Element::SAND, 15);
  for (int x = 20; x < 300; x += 40)
    sim.Paint(x, 130, Element::FIRE, 3);
  for (int i = 0; i < 60; ++i) // warm up, let things fall and catch fire
    sim.Update(1.0f / 60.0f);

  using Clock = std::chrono::steady_clock;
  constexpr int FRAMES = 240;
  auto start = Clock::now();
  double worst = 0;
  for (int i = 0; i < FRAMES; ++i) {
    auto t0 = Clock::now();
    sim.Update(1.0f / 60.0f);
    worst = std::max(worst, std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
  }
  double avg = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / FRAMES;
  std::printf("sim step: avg %.2f ms, worst %.2f ms (budget 16.7)\n", avg, worst);
}
