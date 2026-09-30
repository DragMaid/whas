using System.Text.Json;
using Whas.Server.Spells;

namespace Whas.Server.Matches;

// Checks a revealed TurnPlan (the JSON from the C++ PlanCodec) against the
// rules the client's TurnController enforces, so a modified client can't
// move while channelling, overrun the turn clock or cast spells it didn't
// bring:
//   {"v":1,"runs":[{"n":12,"in":2},{"n":1,"in":0,"casts":[{"id":7,"ax":..,"ay":..}]}]}
public static class PlanValidator
{
    public const int PlanVersion = 1;
    public const int TurnTicks = 180;
    const int AimScale = SpellEvaluator.AimScale - 1; // unit length on the wire
    const double AimTolerance = 0.02;
    const int MaxCastsPerStep = 8;

    public sealed record Result(bool Ok, string? Error, IReadOnlyList<long> CastSpellIds)
    {
        public static Result Fail(string error) => new(false, error, []);
    }

    public const string EmptyPlan = """{"v":1,"runs":[]}""";

    public static Result Validate(string planJson,
                                  IReadOnlyDictionary<long, QuantizedStats> deck)
    {
        JsonDocument doc;
        try { doc = JsonDocument.Parse(planJson); }
        catch (JsonException) { return Result.Fail("plan is not JSON"); }

        using (doc)
        {
            var root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object ||
                !root.TryGetProperty("v", out var v) || v.ValueKind != JsonValueKind.Number ||
                v.GetInt32() != PlanVersion)
                return Result.Fail("unsupported plan version");
            if (!root.TryGetProperty("runs", out var runs) || runs.ValueKind != JsonValueKind.Array)
                return Result.Fail("plan has no runs");

            var casts = new List<long>();
            int steps = 0;
            int owed = 0; // channel ticks still to be spent standing still
            foreach (var run in runs.EnumerateArray())
            {
                if (!TryInt(run, "n", out int n) || n < 1 || !TryInt(run, "in", out int input) ||
                    input < 0 || input > 7)
                    return Result.Fail("bad run");
                if (steps + n > TurnTicks)
                    return Result.Fail("plan is longer than a turn");
                // The cursor sights set spells follow: two int16s
                if (run.TryGetProperty("c", out var cursor) &&
                    (cursor.ValueKind != JsonValueKind.Array || cursor.GetArrayLength() != 2 ||
                     cursor.EnumerateArray().Any(v => v.ValueKind != JsonValueKind.Number ||
                                                      !v.TryGetInt16(out _))))
                    return Result.Fail("bad cursor");

                if (run.TryGetProperty("casts", out var castList))
                {
                    if (n != 1 || castList.ValueKind != JsonValueKind.Array)
                        return Result.Fail("casts on a multi-step run");
                    int count = 0, flights = 0;
                    foreach (var cast in castList.EnumerateArray())
                    {
                        if (++count > MaxCastsPerStep)
                            return Result.Fail("too many casts in one step");
                        if (!TryLong(cast, "id", out long id) || !deck.TryGetValue(id, out var stats))
                            return Result.Fail("cast of a spell not in this round's deck");
                        if (!stats.Valid)
                            return Result.Fail("cast of an invalid spell");
                        if (!TryInt(cast, "ax", out int ax) || !TryInt(cast, "ay", out int ay) ||
                            ax < short.MinValue || ax > short.MaxValue ||
                            ay < short.MinValue || ay > short.MaxValue)
                            return Result.Fail("bad aim");
                        double len = Math.Sqrt((double)ax * ax + (double)ay * ay) / AimScale;
                        if (Math.Abs(len - 1.0) > AimTolerance)
                            return Result.Fail("aim is not a unit vector");
                        // Casts in one step were queued in the same pause
                        if (stats.HasFlight && ++flights > 1)
                            return Result.Fail("more than one wind underfoot cast per pause");
                        owed += SpellEvaluator.CastTicks(stats);
                        casts.Add(id);
                    }
                }

                // Channelling keeps the caster still (TurnController::FlowTick)
                for (int i = 0; i < n; ++i)
                {
                    if (owed > 0 && input != 0)
                        return Result.Fail("moving while channelling");
                    owed = Math.Max(0, owed - 1);
                }
                steps += n;
            }

            // Queued casts must have had time to finish
            if (steps + owed > TurnTicks)
                return Result.Fail("casts overrun the turn");
            return new Result(true, null, casts);
        }
    }

    static bool TryInt(JsonElement e, string name, out int value)
    {
        value = 0;
        return e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.Number &&
               p.TryGetInt32(out value);
    }

    static bool TryLong(JsonElement e, string name, out long value)
    {
        value = 0;
        return e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.Number &&
               p.TryGetInt64(out value);
    }
}
