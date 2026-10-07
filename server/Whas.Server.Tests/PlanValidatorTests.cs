using Whas.Server.Matches;
using Whas.Server.Spells;

namespace Whas.Server.Tests;

public class PlanValidatorTests
{
    const int Unit = 16383;

    // Water bolt (18 + 0.3*particles ticks) and a wind (flight) spell
    static readonly Dictionary<long, QuantizedStats> Deck = new()
    {
        [7] = new(true, (byte)SpellKind.Element, 1, 0, 0, 40 * 1024, 32 * 1024, 1024, 1024, 2048, 40, 0, 0, 0, 0),
        [8] = new(true, (byte)SpellKind.Flight, 0, 0, 0, 40 * 1024, 32 * 1024, 0, 0, 2048, 0, 0, 50 * 1024, 0, 0),
        [9] = new(false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    };

    static PlanValidator.Result V(string json) => PlanValidator.Validate(json, Deck);

    [Fact]
    public void AcceptsWalkingThenCasting()
    {
        var r = V($$"""{"v":1,"runs":[{"n":30,"in":2},{"n":1,"in":0,"casts":[{"id":7,"ax":{{Unit}},"ay":0}]},{"n":29,"in":0}]}""");
        Assert.True(r.Ok, r.Error);
        Assert.Equal([7L], r.CastSpellIds);
    }

    [Fact]
    public void AcceptsACursorPath() =>
        Assert.True(V("""{"v":1,"runs":[{"n":3,"in":0,"c":[1280,-40]},{"n":2,"in":2,"c":[1300,-40]}]}""").Ok);

    [Fact]
    public void AcceptsAPlacedCast() =>
        Assert.True(V($$"""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":7,"ax":0,"ay":-{{Unit}},"px":-200,"py":96}]},{"n":29,"in":0}]}""").Ok);

    [Fact]
    public void AcceptsTheEmptyPlan() => Assert.True(V(PlanValidator.EmptyPlan).Ok);

    [Theory]
    [InlineData("""{"v":2,"runs":[]}""", "version")]
    [InlineData("""{"v":1,"runs":[{"n":181,"in":0}]}""", "longer")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":16}]}""", "bad run")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":99,"ax":16383,"ay":0}]}]}""", "deck")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":9,"ax":16383,"ay":0}]}]}""", "invalid")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":7,"ax":9000,"ay":0}]}]}""", "unit")]
    [InlineData("""{"v":1,"runs":[{"n":2,"in":0,"casts":[{"id":7,"ax":16383,"ay":0}]}]}""", "multi-step")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":8,"ax":16383,"ay":0},{"id":8,"ax":0,"ay":-16383}]}]}""", "wind underfoot")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"c":[1]}]}""", "cursor")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"c":[1,99999]}]}""", "cursor")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":7,"ax":16383,"ay":0,"px":400,"py":0}]}]}""", "reach")]
    [InlineData("""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":7,"ax":16383,"ay":0,"px":40}]}]}""", "reach")]
    [InlineData("not json", "JSON")]
    public void RejectsCheats(string plan, string expected)
    {
        var r = V(plan);
        Assert.False(r.Ok);
        Assert.Contains(expected, r.Error);
    }

    [Fact]
    public void RejectsMovingWhileChannelling()
    {
        // The water bolt channels for 30 ticks; walking on the next tick
        var r = V($$"""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":7,"ax":{{Unit}},"ay":0}]},{"n":5,"in":1}]}""");
        Assert.False(r.Ok);
        Assert.Contains("channelling", r.Error);
    }

    [Fact]
    public void RejectsCastsThatOverrunTheTurn()
    {
        var r = V($$"""{"v":1,"runs":[{"n":170,"in":0},{"n":1,"in":0,"casts":[{"id":7,"ax":{{Unit}},"ay":0}]}]}""");
        Assert.False(r.Ok);
        Assert.Contains("overrun", r.Error);
    }
}
