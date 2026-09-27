#include "whas/net/plan_codec.h"

namespace PlanCodec {

namespace {

int InputBits(const CharacterInput &in) {
  return (in.left ? 1 : 0) | (in.right ? 2 : 0) | (in.jump ? 4 : 0);
}

CharacterInput InputFromBits(int bits) {
  CharacterInput in;
  in.left = bits & 1;
  in.right = bits & 2;
  in.jump = bits & 4;
  return in;
}

} // namespace

nlohmann::json Encode(const TurnPlan &plan) {
  nlohmann::json runs = nlohmann::json::array();
  for (const PlanStep &step : plan.steps) {
    int bits = InputBits(step.input);
    if (step.casts.empty() && !runs.empty() && !runs.back().contains("casts") &&
        runs.back()["in"] == bits) {
      runs.back()["n"] = runs.back()["n"].get<int>() + 1;
      continue;
    }
    nlohmann::json run{{"n", 1}, {"in", bits}};
    if (!step.casts.empty()) {
      nlohmann::json casts = nlohmann::json::array();
      for (const PlannedCast &cast : step.casts)
        casts.push_back(
            {{"id", cast.spellId}, {"ax", cast.aimQ.x}, {"ay", cast.aimQ.y}});
      run["casts"] = std::move(casts);
    }
    runs.push_back(std::move(run));
  }
  return {{"v", VERSION}, {"runs", std::move(runs)}};
}

bool Decode(const nlohmann::json &j, const SpellResolver &resolve,
            TurnPlan &plan, std::string &error) {
  plan.steps.clear();
  try {
    if (j.at("v").get<int>() != VERSION) {
      error = "unsupported plan version";
      return false;
    }
    for (const auto &run : j.at("runs")) {
      int n = run.at("n").get<int>();
      int bits = run.at("in").get<int>();
      if (n < 1 || bits < 0 || bits > 7 ||
          plan.steps.size() + n > TurnController::TURN_TICKS) {
        error = "bad run";
        return false;
      }
      PlanStep step;
      step.input = InputFromBits(bits);
      if (run.contains("casts")) {
        if (n != 1) {
          error = "casts on a multi-step run";
          return false;
        }
        for (const auto &c : run.at("casts")) {
          PlannedCast cast;
          cast.spellId = c.at("id").get<int64_t>();
          cast.aimQ = {c.at("ax").get<int16_t>(), c.at("ay").get<int16_t>()};
          cast.aim = SpellQuant::DequantizeAim(cast.aimQ);
          if (!resolve(cast.spellId, cast.spell, cast.stats)) {
            error = "unknown spell id";
            return false;
          }
          step.casts.push_back(std::move(cast));
        }
      }
      for (int i = 0; i < n; ++i)
        plan.steps.push_back(step);
    }
  } catch (const nlohmann::json::exception &e) {
    error = e.what();
    return false;
  }
  return true;
}

} // namespace PlanCodec
