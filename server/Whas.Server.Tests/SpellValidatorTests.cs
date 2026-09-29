using Whas.Server.Spells;

namespace Whas.Server.Tests;

public class SpellValidatorTests
{
    static Glyph Sigil(string id) => new(id, "sigil", 0, 0, 1, 0);
    static Glyph Sign(string id, bool inverted = false) => new(id, "sign", 0, -120, 1, 0, inverted);

    static readonly List<Glyph> Water = [Sigil("water"), Sign("column")];

    static Component Part(List<Glyph> glyphs, float x = 0, float scale = 0.3f) =>
        new("part", x, 0, scale, 0, glyphs);

    [Fact]
    public void AcceptsTheNewSignsAndTheDragon()
    {
        List<Glyph> glyphs =
        [
            Sigil("fire"), Sigil("dragon"), Sign("column"), Sign("convergence"),
            Sign("crushing", inverted: true), Sign("repetition"), Sign("cooling"),
            Sign("strengthening"), Sign("collection"), Sign("expansion", inverted: true),
            Sign("orb"),
        ];
        Assert.Null(SpellValidator.Check("all", glyphs));
    }

    [Theory]
    [InlineData("cooling", "inverted")]
    [InlineData("column", "inverted")]
    public void OnlyCrushingAndExpansionInvert(string sign, string reason)
    {
        List<Glyph> glyphs = [Sigil("water"), Sign(sign, inverted: true)];
        Assert.Contains(reason, SpellValidator.Check("x", glyphs));
    }

    [Fact]
    public void RejectsUnknownSignsAndDragonsWithoutAnElement()
    {
        Assert.Contains("unknown sign", SpellValidator.Check("x", [Sigil("water"), Sign("bogus")]));
        Assert.Contains("sigil", SpellValidator.Check("x", [Sigil("dragon"), Sign("column")]));
        Assert.Contains("dragon", SpellValidator.Check("x",
            [Sigil("water"), Sigil("dragon"), Sigil("dragon"), Sign("column")]));
    }

    [Fact]
    public void LayeredSpellsHoldOneToFivePlainSpells()
    {
        Assert.Null(SpellValidator.Check("x", [Sign("cooling")],
            [Part(Water, -60, 0.2f), Part(Water, 60, 0.2f)]));
        Assert.Null(SpellValidator.Check("x", [], [Part(Water)]));
        var six = Enumerable.Range(0, 6).Select(_ => Part(Water, 0, 0.2f)).ToList();
        Assert.Contains("1-5", SpellValidator.Check("x", [], six));
    }

    [Fact]
    public void LayeredSpellsKeepSigilsOutOfTheRingAndPartsInTheCore()
    {
        Assert.Contains("signs only", SpellValidator.Check("x", [Sigil("fire")], [Part(Water)]));
        Assert.Contains("core", SpellValidator.Check("x", [], [Part(Water, 100, 0.5f)]));
        Assert.Contains("scale", SpellValidator.Check("x", [], [Part(Water, 0, 0.9f)]));
        Assert.Contains("layer: a spell needs exactly one sigil",
            SpellValidator.Check("x", [], [Part([Sign("column")])]));
    }
}
