using Whas.Server.Spells;

namespace Whas.Server.Tests;

public class SpellValidatorTests
{
    static Glyph Sigil(string id) => new(id, "sigil", 0, 0, 1, 0);
    static Glyph Sign(string id, bool inverted = false) => new(id, "sign", 0, -120, 1, 0, inverted);

    static readonly List<Glyph> Water = [Sigil("water"), Sign("levitation")];

    static Component Part(List<Glyph> glyphs, float x = 0, float scale = 0.3f) =>
        new("part", x, 0, scale, 0, glyphs);

    [Fact]
    public void SpellsOverTheOrdinaryLimitsAreChaosOnly()
    {
        List<Glyph> full = [Sigil("water"), .. Enumerable.Repeat(Sign("levitation"), SpellValidator.MaxSigns)];
        Assert.Null(SpellValidator.Check("full", full));
        Assert.False(SpellValidator.OverLimit(full, []));
        // Over the limit is still a spell, for chaos rooms
        List<Glyph> over = [.. full, Sign("levitation")];
        Assert.Null(SpellValidator.Check("over", over));
        Assert.True(SpellValidator.OverLimit(over, []));

        var six = Enumerable.Range(0, 6).Select(i => Part(Water, scale: 0.2f)).ToList();
        Assert.Null(SpellValidator.Check("six", [Sign("levitation")], six));
        Assert.True(SpellValidator.OverLimit([Sign("levitation")], six));
        Assert.False(SpellValidator.OverLimit([Sign("levitation")], six.Take(5).ToList()));
    }

    [Fact]
    public void AcceptsTheNewSignsAndTheDragon()
    {
        List<Glyph> glyphs =
        [
            Sigil("fire"), Sigil("dragon"), Sign("levitation"), Sign("convergence"),
            Sign("crushing", inverted: true), Sign("repetition"), Sign("cooling"),
            Sign("strengthening"), Sign("collection"), Sign("expansion", inverted: true),
            Sign("orb"),
        ];
        Assert.Null(SpellValidator.Check("all", glyphs));
    }

    [Theory]
    [InlineData("cooling", "inverted")]
    [InlineData("levitation", "inverted")]
    public void OnlyCrushingAndExpansionInvert(string sign, string reason)
    {
        List<Glyph> glyphs = [Sigil("water"), Sign(sign, inverted: true)];
        Assert.Contains(reason, SpellValidator.Check("x", glyphs));
    }

    [Fact]
    public void RejectsUnknownSignsAndDragonsWithoutAnElement()
    {
        Assert.Contains("unknown sign", SpellValidator.Check("x", [Sigil("water"), Sign("bogus")]));
        Assert.Contains("sigil", SpellValidator.Check("x", [Sigil("dragon"), Sign("levitation")]));
        Assert.Contains("dragon", SpellValidator.Check("x",
            [Sigil("water"), Sigil("dragon"), Sigil("dragon"), Sign("levitation")]));
    }

    [Fact]
    public void LayeredSpellsHoldPlainSpellsUpToTheCeiling()
    {
        Assert.Null(SpellValidator.Check("x", [Sign("cooling")],
            [Part(Water, -60, 0.2f), Part(Water, 60, 0.2f)]));
        Assert.Null(SpellValidator.Check("x", [], [Part(Water)]));
        // Past five it's a chaos-room spell, past the ceiling no spell at all
        var six = Enumerable.Range(0, 6).Select(_ => Part(Water, 0, 0.2f)).ToList();
        Assert.Null(SpellValidator.Check("x", [], six));
        var tooMany = Enumerable.Range(0, SpellEvaluator.MaxComponents + 1)
                                .Select(_ => Part(Water, 0, 0.2f)).ToList();
        Assert.Contains("at most", SpellValidator.Check("x", [], tooMany));
    }

    [Fact]
    public void LayeredSpellsKeepSigilsOutOfTheRingAndPartsInTheCore()
    {
        Assert.Contains("signs only", SpellValidator.Check("x", [Sigil("fire")], [Part(Water)]));
        Assert.Contains("core", SpellValidator.Check("x", [], [Part(Water, 100, 0.5f)]));
        Assert.Contains("scale", SpellValidator.Check("x", [], [Part(Water, 0, 0.9f)]));
        Assert.Contains("layer: a spell needs one sigil to fire",
            SpellValidator.Check("x", [], [Part([Sign("levitation")])]));
    }
}
