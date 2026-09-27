using System.Text.Json;
using Whas.Server.Net;
using Whas.Server.Spells;

namespace Whas.Server.Tests;

// The C++ client and this server must turn the same glyphs into exactly the
// same quantized stats. tests/fixtures/spells.json is written by the C++
// evaluator (whas_tests "[.generate]") and checked by both test suites.
public class SpellGoldenTests
{
    sealed record Case(string Name, List<Glyph> Glyphs, QuantizedStats Stats);
    sealed record Fixture(int EvaluatorVersion, List<Case> Cases);

    static Fixture Load()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "tests/fixtures/spells.json")))
            dir = dir.Parent;
        Assert.NotNull(dir);
        string json = File.ReadAllText(Path.Combine(dir!.FullName, "tests/fixtures/spells.json"));
        return JsonSerializer.Deserialize<Fixture>(json, Protocol.Json)!;
    }

    [Fact]
    public void EvaluatorVersionMatchesTheClient()
    {
        Assert.Equal(SpellEvaluator.Version, Load().EvaluatorVersion);
    }

    [Fact]
    public void EveryGoldenSpellEvaluatesIdentically()
    {
        var fixture = Load();
        Assert.True(fixture.Cases.Count > 300);
        var mismatches = fixture.Cases
            .Where(c => SpellEvaluator.EvaluateQuantized(c.Glyphs) != c.Stats)
            .Select(c => $"{c.Name}: expected {c.Stats}, got {SpellEvaluator.EvaluateQuantized(c.Glyphs)}")
            .ToList();
        Assert.True(mismatches.Count == 0, string.Join("\n", mismatches.Take(10)));
    }

    [Fact]
    public void CastTicksMatchTheClientFormula()
    {
        Assert.Equal(18, SpellEvaluator.CastTicks(Stats(particles: 0)));
        Assert.Equal(32, SpellEvaluator.CastTicks(Stats(particles: 45))); // 31.5 rounds up
        Assert.Equal(78, SpellEvaluator.CastTicks(Stats(particles: 200)));
    }

    static QuantizedStats Stats(int particles) =>
        new(true, 1, 1, 0, 0, 0, 0, 0, 0, 0, particles, 0, 0, 0, 0);
}
