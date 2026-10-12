#pragma once
#include "whas/game/turn_controller.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

// TurnPlan <-> JSON for the wire and for replays. Runs of identical input
// are collapsed, casts carry the server spell id and the quantized aim:
//   {"v":1,"runs":[{"n":12,"in":2},{"n":1,"in":0,"casts":[{"id":7,"ax":..,"ay":..}]}]}
// "in" bits: 1 left, 2 right, 4 jump. A run may carry "c":[x,y], the cursor
// in 1/8 cells (sights set). A placed cast adds "px","py": where it is drawn,
// from the caster's centre in 1/8 cells. See docs/protocol.md.
namespace PlanCodec {

constexpr int VERSION = 1;

nlohmann::json Encode(const TurnPlan &plan);

// Looks up a spell id: the spell (for drawing) and its authoritative stats
using SpellResolver =
    std::function<bool(int64_t id, Spell &spell, SpellStats &stats)>;

bool Decode(const nlohmann::json &j, const SpellResolver &resolve,
            TurnPlan &plan, std::string &error);

} // namespace PlanCodec
