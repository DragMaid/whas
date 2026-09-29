using System.Text.Json;
using Whas.Server.Net;
using Whas.Server.Spells;

namespace Whas.Server.Tests;

// The C++ client and this server must turn the same glyphs into exactly the
// same quantized stats. tests/fixtures/spells.json is written by the C++
// evaluator (whas_tests "[.generate]") and checked by both test suites.
public class SpellGoldenTests
{
    sealed record Case(string Name, List<Glyph> Glyphs, QuantizedStats Stats,
                       List<Component>? Components);
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
            .Where(c => Evaluate(c) != c.Stats)
            .Select(c => $"{c.Name}: expected {c.Stats}, got {Evaluate(c)}")
            .ToList();
        Assert.True(mismatches.Count == 0, string.Join("\n", mismatches.Take(10)));
    }

    static QuantizedStats Evaluate(Case c) =>
        SpellEvaluator.EvaluateQuantized(c.Glyphs, c.Components ?? []);

    [Fact]
    public void GoldenCasesCoverLayeredSpellsAndModifiers()
    {
        var fixture = Load();
        Assert.Contains(fixture.Cases, c => c.Stats.Kind == (byte)SpellKind.Compound && c.Stats.Valid);
        Assert.Contains(fixture.Cases, c => c.Glyphs.Any(g => g.Inverted));
        Assert.Contains(fixture.Cases, c => c.Stats.Shape == (byte)SpellShape.Dragon);
    }

    [Fact]
    public void LayeredCastTicksTakeTheSlowestPartAndHalfTheRest()
    {
        // Parts of 18, 32 and 78 ticks: 78 + ceil((18 + 32) / 2) + 8 + 2 * 2
        var layered = Stats(particles: 0) with
        {
            Kind = (byte)SpellKind.Compound,
            Parts = [Stats(particles: 0), Stats(particles: 45), Stats(particles: 200)],
        };
        Assert.Equal(78 + 25 + 8 + 4, SpellEvaluator.CastTicks(layered));
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
