using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Whas.Server.Data;
using Whas.Server.Spells;

namespace Whas.Server.Tests;

[Collection("server")]
public class MatchFlowTests(ServerFixture server)
{
    const int Unit = 16383;

    sealed record Player(TestClient Client, long WaterId, long WindId, long DeckId)
    {
        public int Slot { get; set; } = -1;
    }

    static readonly object[] WaterGlyphs =
    [
        new { assetId = "water", kind = "sigil", x = 0f, y = 0f, scale = 1f, rotation = 0f },
        new { assetId = "levitation", kind = "sign", x = 0f, y = -120f, scale = 1f, rotation = 0f },
    ];

    static readonly object[] WindGlyphs =
    [
        new { assetId = "wind_underfoot", kind = "sigil", x = 0f, y = 0f, scale = 1f, rotation = 0f },
        new { assetId = "levitation", kind = "sign", x = 0f, y = -120f, scale = 1f, rotation = 0f },
    ];

    async Task<Player> NewPlayerAsync(string build)
    {
        var c = await TestClient.ConnectAsync(server, build);
        await c.SendAsync(new { type = "uploadSpell", @ref = "w", name = "Water bolt", glyphs = WaterGlyphs });
        long water = (await c.ExpectAsync("spellAccepted")).GetProperty("spellId").GetInt64();
        await c.SendAsync(new { type = "uploadSpell", @ref = "f", name = "Hop", glyphs = WindGlyphs });
        long wind = (await c.ExpectAsync("spellAccepted")).GetProperty("spellId").GetInt64();
        await c.SendAsync(new { type = "upsertDeck", @ref = "d", name = "Deck", spellIds = new[] { water, wind, 0, 0, 0, 0 } });
        long deck = (await c.ExpectAsync("deckAccepted")).GetProperty("deckId").GetInt64();
        return new Player(c, water, wind, deck);
    }

    // Two players matched through the queue, decks locked, round 1 started.
    // Returned in slot order.
    async Task<(Player P0, Player P1, long MatchId)> StartMatchAsync()
    {
        string build = "test-" + Guid.NewGuid();
        var a = await NewPlayerAsync(build);
        var b = await NewPlayerAsync(build);
        await a.Client.SendAsync(new { type = "queue" });
        await a.Client.ExpectAsync("queued");
        await b.Client.SendAsync(new { type = "queue" });
        var foundA = await a.Client.ExpectAsync("matchFound");
        var foundB = await b.Client.ExpectAsync("matchFound");
        a.Slot = foundA.GetProperty("slot").GetInt32();
        b.Slot = foundB.GetProperty("slot").GetInt32();
        Assert.Equal(1, a.Slot + b.Slot);
        Assert.Equal(foundA.GetProperty("seed").GetString(), foundB.GetProperty("seed").GetString());

        foreach (var p in new[] { a, b })
        {
            await p.Client.SendAsync(new { type = "matchDecks", deckIds = new[] { p.DeckId, p.DeckId, p.DeckId } });
            await p.Client.ExpectAsync("decksLocked");
        }
        foreach (var p in new[] { a, b })
        {
            var start = await p.Client.ExpectAsync("roundStart");
            Assert.Equal(0, start.GetProperty("round").GetInt32());
            // Both players see both decks, with server-computed stats
            Assert.Equal(2, start.GetProperty("decks").GetArrayLength());
        }
        var (p0, p1) = a.Slot == 0 ? (a, b) : (b, a);
        return (p0, p1, foundA.GetProperty("matchId").GetInt64());
    }

    static string Hash(string plan, string nonce) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(plan + nonce))).ToLowerInvariant();

    static string CastPlan(long spellId) =>
        $$"""{"v":1,"runs":[{"n":10,"in":2},{"n":1,"in":0,"casts":[{"id":{{spellId}},"ax":{{Unit}},"ay":0}]}]}""";

    static async Task CommitAsync(Player p, int round, int turn, string plan, string nonce)
    {
        await p.Client.ExpectAsync("turnStart");
        await p.Client.SendAsync(new { type = "commit", round, turn, hash = Hash(plan, nonce) });
    }

    static async Task RevealAsync(Player p, int round, int turn, string plan, string nonce)
    {
        await p.Client.ExpectAsync("bothCommitted");
        await p.Client.SendAsync(new { type = "reveal", round, turn, plan, nonce });
    }

    // Both play a turn and report the given hashes/winner; returns turnPlans
    async Task<JsonElement> TurnAsync(Player p0, Player p1, int round, int turn,
                                      string h0 = "abc", string h1 = "abc", int winner = -1)
    {
        string plan0 = CastPlan(p0.WaterId), plan1 = CastPlan(p1.WaterId);
        await CommitAsync(p0, round, turn, plan0, "n0");
        await CommitAsync(p1, round, turn, plan1, "n1");
        await RevealAsync(p0, round, turn, plan0, "n0");
        await RevealAsync(p1, round, turn, plan1, "n1");
        var plans = await p0.Client.ExpectAsync("turnPlans");
        await p1.Client.ExpectAsync("turnPlans");
        await p0.Client.SendAsync(new { type = "stateHash", round, turn, hash = h0, winner });
        await p1.Client.SendAsync(new { type = "stateHash", round, turn, hash = h1, winner });
        return plans;
    }

    WhasDb Db() => server.Factory.Services.GetRequiredService<IDbContextFactory<WhasDb>>().CreateDbContext();

    [Fact]
    public async Task FullBestOfThreeIsRecorded()
    {
        var (p0, p1, matchId) = await StartMatchAsync();

        // Round 1: one quiet turn, then slot 0 wins
        var plans = await TurnAsync(p0, p1, 0, 0);
        Assert.Equal(CastPlan(p0.WaterId), plans.GetProperty("plans")[0].GetString());
        Assert.False(plans.GetProperty("substituted")[1].GetBoolean());
        await TurnAsync(p0, p1, 0, 1, winner: 0);
        var end1 = await p1.Client.ExpectAsync("roundEnd");
        Assert.Equal(0, end1.GetProperty("winner").GetInt32());

        // Round 2: slot 0 again, which settles the match 2-0
        await p0.Client.ExpectAsync("roundStart");
        await p1.Client.ExpectAsync("roundStart");
        await TurnAsync(p0, p1, 1, 0, winner: 0);
        var matchEnd = await p1.Client.ExpectAsync("matchEnd");
        Assert.Equal(0, matchEnd.GetProperty("winner").GetInt32());
        Assert.Equal("Finished", matchEnd.GetProperty("status").GetString());

        await using var db = Db();
        var match = await WaitForAsync(() => db.Matches.AsNoTracking()
            .FirstAsync(m => m.Id == matchId), m => m.EndedAt is not null);
        Assert.Equal(MatchStatus.Finished, match.Status);
        Assert.Equal(p0.Client.PlayerId, match.WinnerId);
        Assert.Equal(3, await db.Turns.CountAsync(t => t.MatchId == matchId));
        Assert.All(await db.Turns.Where(t => t.MatchId == matchId).ToListAsync(),
                   t => Assert.Equal("abc", t.HashSlot0));
        var usage = await db.SpellUsage.Where(u => u.MatchId == matchId).ToListAsync();
        Assert.Equal(3, usage.Single(u => u.SpellId == p0.WaterId).Casts);
        Assert.Equal(1, (await db.Players.FindAsync(p0.Client.PlayerId))!.Wins);

        // The replay endpoint has everything needed to re-simulate
        var http = server.Factory.CreateClient();
        http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", p1.Client.Token);
        var replay = await http.GetFromJsonAsync<JsonElement>($"/api/matches/{matchId}/replay");
        Assert.Equal(3, replay.GetProperty("turns").GetArrayLength());
        Assert.Equal(2, replay.GetProperty("players").GetArrayLength());
        var history = await http.GetFromJsonAsync<JsonElement>("/api/players/me/matches");
        Assert.Contains(history.EnumerateArray(), m => m.GetProperty("matchId").GetInt64() == matchId);

        http.DefaultRequestHeaders.Authorization = null;
        Assert.Equal(HttpStatusCode.Unauthorized, (await http.GetAsync("/api/decks/mine")).StatusCode);
    }

    [Fact]
    public async Task TamperedAndMissingPlansAreReplacedWithStandingStill()
    {
        var (p0, p1, _) = await StartMatchAsync();
        string plan0 = CastPlan(p0.WaterId);
        // Slot 1 casts a spell that isn't in its deck (slot 0's spell id)
        string cheat = CastPlan(p0.WaterId);
        await CommitAsync(p0, 0, 0, plan0, "a");
        await CommitAsync(p1, 0, 0, cheat, "b");
        await RevealAsync(p0, 0, 0, plan0, "a");
        await RevealAsync(p1, 0, 0, cheat, "b");
        var rejected = await p1.Client.ExpectAsync("planRejected");
        Assert.Contains("deck", rejected.GetProperty("reason").GetString());
        var plans = await p0.Client.ExpectAsync("turnPlans");
        Assert.True(plans.GetProperty("substituted")[1].GetBoolean());
        Assert.Equal("""{"v":1,"runs":[]}""", plans.GetProperty("plans")[1].GetString());
        await p1.Client.ExpectAsync("turnPlans");
        await p0.Client.SendAsync(new { type = "stateHash", round = 0, turn = 0, hash = "h", winner = -1 });
        await p1.Client.SendAsync(new { type = "stateHash", round = 0, turn = 0, hash = "h", winner = -1 });

        // Next turn slot 0 reveals something other than what it committed,
        // and slot 1 never commits at all
        await CommitAsync(p0, 0, 1, plan0, "a");
        await p1.Client.ExpectAsync("turnStart");
        await RevealAsync(p0, 0, 1, CastPlan(p0.WindId), "a");
        Assert.Contains("commit", (await p0.Client.ExpectAsync("planRejected")).GetProperty("reason").GetString());
        plans = await p1.Client.ExpectAsync("turnPlans", seconds: 8);
        Assert.True(plans.GetProperty("substituted")[0].GetBoolean());
        Assert.True(plans.GetProperty("substituted")[1].GetBoolean());
    }

    [Fact]
    public async Task DisagreeingHashesResyncFromTheSeededReference()
    {
        var (p0, p1, matchId) = await StartMatchAsync();
        await TurnAsync(p0, p1, 0, 0, h0: "aaaa", h1: "bbbb");
        var desync = await p0.Client.ExpectAsync("desync");
        await p1.Client.ExpectAsync("desync");
        int reference = desync.GetProperty("referenceSlot").GetInt32();
        var (refPlayer, other) = reference == 0 ? (p0, p1) : (p1, p0);

        await refPlayer.Client.SendAsync(new { type = "snapshot", round = 0, turn = 0, data = "WORLD" });
        var snapshot = await other.Client.ExpectAsync("snapshot");
        Assert.Equal("WORLD", snapshot.GetProperty("data").GetString());
        Assert.Equal(reference == 0 ? "aaaa" : "bbbb", snapshot.GetProperty("hash").GetString());

        // Play goes on
        await p0.Client.ExpectAsync("turnStart");
        await using var db = Db();
        var report = await db.DesyncReports.SingleAsync(r => r.MatchId == matchId);
        Assert.Equal(reference, report.ReferenceSlot);
        Assert.Equal(other.Client.PlayerId, report.SuspectPlayerId);
    }

    [Fact]
    public async Task ADroppedPlayerCanRejoinAndCatchUp()
    {
        var (p0, p1, matchId) = await StartMatchAsync();
        await TurnAsync(p0, p1, 0, 0);

        string build = p1.Client.Welcome.GetProperty("playerId").ToString();
        string token = p1.Client.Token;
        await p1.Client.DisposeAsync();
        var gone = await p0.Client.ExpectAsync("opponentDisconnected");
        Assert.True(gone.GetProperty("graceSec").GetInt32() > 0);

        await using var back = await TestClient.ConnectAsync(server, "any", token);
        Assert.Equal(matchId, back.Welcome.GetProperty("runningMatch").GetInt64());
        await back.SendAsync(new { type = "rejoin", matchId });
        var catchUp = await back.ExpectAsync("catchUp");
        Assert.Equal(p1.Slot, catchUp.GetProperty("slot").GetInt32());
        Assert.Equal(1, catchUp.GetProperty("turns").GetArrayLength());
        Assert.Equal("commit", catchUp.GetProperty("current").GetProperty("phase").GetString());
        await p0.Client.ExpectAsync("opponentReconnected");
    }

    [Fact]
    public async Task PlayersCanOpenARoomRightAfterAMatch()
    {
        var (p0, p1, _) = await StartMatchAsync();
        await p1.Client.SendAsync(new { type = "leave" });
        await p0.Client.ExpectAsync("matchEnd");
        await p1.Client.ExpectAsync("matchEnd");

        await p0.Client.SendAsync(new { type = "createLobby" });
        await p0.Client.ExpectAsync("lobbyCreated");
        await p1.Client.SendAsync(new { type = "queue" });
        await p1.Client.ExpectAsync("queued");
    }

    [Fact]
    public async Task NotComingBackForfeitsTheMatch()
    {
        var (p0, p1, matchId) = await StartMatchAsync();
        await p1.Client.DisposeAsync();
        await p0.Client.ExpectAsync("opponentDisconnected");
        var end = await p0.Client.ExpectAsync("matchEnd", seconds: 10);
        Assert.Equal(0, end.GetProperty("winner").GetInt32());
        Assert.Equal("Forfeit", end.GetProperty("status").GetString());

        await using var db = Db();
        var match = await WaitForAsync(() => db.Matches.AsNoTracking().FirstAsync(m => m.Id == matchId),
                                       m => m.EndedAt is not null);
        Assert.Equal(MatchStatus.Forfeit, match.Status);
    }

    [Fact]
    public async Task LobbyCodesPairFriendsOnTheSameBuild()
    {
        string build = "lobby-" + Guid.NewGuid();
        var host = await NewPlayerAsync(build);
        var friend = await NewPlayerAsync(build);
        var stranger = await NewPlayerAsync("other-build");

        await host.Client.SendAsync(new { type = "createLobby" });
        string code = (await host.Client.ExpectAsync("lobbyCreated")).GetProperty("code").GetString()!;
        Assert.Equal(6, code.Length);

        await stranger.Client.SendAsync(new { type = "joinLobby", code });
        Assert.Contains("build", (await stranger.Client.ExpectAsync("error")).GetProperty("message").GetString());

        await friend.Client.SendAsync(new { type = "joinLobby", code = code.ToLowerInvariant() });
        var found = await friend.Client.ExpectAsync("matchFound");
        Assert.Equal("Lobby", found.GetProperty("mode").GetString());
        await host.Client.ExpectAsync("matchFound");
    }

    [Fact]
    public async Task LobbyMapPoolsReachBothPlayersAndTheReplay()
    {
        string build = "maps-" + Guid.NewGuid();
        var host = await NewPlayerAsync(build);
        var friend = await NewPlayerAsync(build);

        var tooMany = new object[] { new { kind = "random" }, new { kind = "random" },
                                     new { kind = "random" }, new { kind = "random" } };
        await host.Client.SendAsync(new { type = "createLobby", options = new { maps = tooMany } });
        Assert.Contains("at most", (await host.Client.ExpectAsync("error")).GetProperty("message").GetString());

        var huge = new { kind = "custom", map = new { cells = new string('A', 70 * 1024) } };
        await host.Client.SendAsync(new { type = "createLobby", options = new { maps = new object[] { huge } } });
        Assert.Contains("too large", (await host.Client.ExpectAsync("error")).GetProperty("message").GetString());

        var custom = new { kind = "custom", map = new { format = 1, name = "Pond" } };
        await host.Client.SendAsync(new
        {
            type = "createLobby",
            options = new { maps = new object[] { custom, new { kind = "random" } }, chaos = true },
        });
        string code = (await host.Client.ExpectAsync("lobbyCreated")).GetProperty("code").GetString()!;
        await friend.Client.SendAsync(new { type = "joinLobby", code });
        foreach (var p in new[] { host, friend })
        {
            var options = (await p.Client.ExpectAsync("matchFound")).GetProperty("options");
            var maps = options.GetProperty("maps");
            Assert.Equal(2, maps.GetArrayLength());
            Assert.Equal("Pond", maps[0].GetProperty("map").GetProperty("name").GetString());
            Assert.Equal("random", maps[1].GetProperty("kind").GetString());
            Assert.True(options.GetProperty("chaos").GetBoolean());
            Assert.False(options.GetProperty("rts").GetBoolean());
        }
    }

    [Fact]
    public async Task SpellsOverTheLimitOnlyPlayInChaosRooms()
    {
        foreach (bool chaos in new[] { false, true })
        {
            string build = "chaos-" + Guid.NewGuid();
            var host = await NewPlayerAsync(build);
            var friend = await NewPlayerAsync(build);

            // 33 signs: accepted, but over the ordinary limit
            var signs = Enumerable.Repeat<object>(
                new { assetId = "levitation", kind = "sign", x = 0f, y = -120f, scale = 0.1f, rotation = 0f }, 33);
            object[] glyphs = [new { assetId = "water", kind = "sigil", x = 0f, y = 0f, scale = 1f, rotation = 0f },
                               .. signs];
            await host.Client.SendAsync(new { type = "uploadSpell", @ref = "big", name = "Deluge", glyphs });
            long big = (await host.Client.ExpectAsync("spellAccepted")).GetProperty("spellId").GetInt64();
            await host.Client.SendAsync(new
            {
                type = "upsertDeck", @ref = "c", name = "Chaos",
                spellIds = new[] { big, host.WaterId, 0, 0, 0, 0 },
            });
            long deck = (await host.Client.ExpectAsync("deckAccepted")).GetProperty("deckId").GetInt64();

            await host.Client.SendAsync(new { type = "createLobby", options = new { chaos } });
            string code = (await host.Client.ExpectAsync("lobbyCreated")).GetProperty("code").GetString()!;
            await friend.Client.SendAsync(new { type = "joinLobby", code });
            await host.Client.ExpectAsync("matchFound");
            await friend.Client.ExpectAsync("matchFound");

            await host.Client.SendAsync(new { type = "matchDecks", deckIds = new[] { deck, deck, deck } });
            var locked = await host.Client.ExpectAsync("decksLocked");
            var round = locked.GetProperty("rounds")[0];
            Assert.Equal(chaos ? JsonValueKind.Object : JsonValueKind.Null, round[0].ValueKind);
            Assert.Equal(JsonValueKind.Object, round[1].ValueKind);
            await host.Client.SendAsync(new { type = "leave" });
        }
    }

    static string BatchPlan(long spellId = 0) => spellId == 0
        ? """{"v":1,"runs":[{"n":6,"in":2}]}"""
        : $$"""{"v":1,"runs":[{"n":1,"in":0,"casts":[{"id":{{spellId}},"ax":{{Unit}},"ay":0}]},{"n":5,"in":1}]}""";

    [Fact]
    public async Task RealTimeRoomsStreamInputBatches()
    {
        string build = "rts-" + Guid.NewGuid();
        var host = await NewPlayerAsync(build);
        var friend = await NewPlayerAsync(build);
        await host.Client.SendAsync(new { type = "createLobby", options = new { rts = true } });
        string code = (await host.Client.ExpectAsync("lobbyCreated")).GetProperty("code").GetString()!;
        await friend.Client.SendAsync(new { type = "joinLobby", code });
        foreach (var p in new[] { host, friend })
        {
            var found = await p.Client.ExpectAsync("matchFound");
            Assert.True(found.GetProperty("options").GetProperty("rts").GetBoolean());
            p.Slot = found.GetProperty("slot").GetInt32();
            await p.Client.SendAsync(new { type = "matchDecks", deckIds = new[] { p.DeckId, p.DeckId, p.DeckId } });
            await p.Client.ExpectAsync("decksLocked");
        }
        foreach (var p in new[] { host, friend })
            await p.Client.ExpectAsync("roundStart");
        var (p0, p1) = host.Slot == 0 ? (host, friend) : (friend, host);

        // Batch 0: slot 0 casts, slot 1 walks. A batch goes out once both are in.
        await p0.Client.SendAsync(new { type = "inputs", round = 0, batch = 0, plan = BatchPlan(p0.WaterId) });
        await p1.Client.SendAsync(new { type = "inputs", round = 0, batch = 0, plan = BatchPlan() });
        var frames = await p0.Client.ExpectAsync("frames");
        Assert.Equal(0, frames.GetProperty("batch").GetInt32());
        Assert.Equal(BatchPlan(p0.WaterId), frames.GetProperty("plans")[0].GetString());
        await p1.Client.ExpectAsync("frames");

        // Batch 1: casting the same spell again is too soon
        await p0.Client.SendAsync(new { type = "inputs", round = 0, batch = 1, plan = BatchPlan(p0.WaterId) });
        await p1.Client.SendAsync(new { type = "inputs", round = 0, batch = 1, plan = BatchPlan() });
        Assert.Contains("cooling", (await p0.Client.ExpectAsync("planRejected")).GetProperty("reason").GetString());
        frames = await p0.Client.ExpectAsync("frames");
        Assert.True(frames.GetProperty("substituted")[0].GetBoolean());
        await p1.Client.ExpectAsync("frames");

        // Both report slot 1 down after batch 1: the round goes to slot 0
        foreach (var p in new[] { p0, p1 })
            await p.Client.SendAsync(new { type = "stateHash", round = 0, turn = 1, hash = "same", winner = 0 });
        var end = await p0.Client.ExpectAsync("roundEnd");
        Assert.Equal(0, end.GetProperty("winner").GetInt32());

        using var db = Db();
        var matchId = await db.Matches.Where(m => m.Players.Any(pl => pl.PlayerId == p0.Client.PlayerId))
                                      .OrderByDescending(m => m.Id).Select(m => m.Id).FirstAsync();
        var turns = await db.Turns.Where(t => t.MatchId == matchId).OrderBy(t => t.Turn).ToListAsync();
        Assert.Equal(2, turns.Count);
        Assert.Equal("same", turns[1].HashSlot0);
    }

    [Fact]
    public async Task UploadsAreValidatedAndStatsComeFromTheServer()
    {
        var c = await TestClient.ConnectAsync(server, "b");
        await c.SendAsync(new
        {
            type = "uploadSpell", @ref = "x", name = "Two sigils",
            glyphs = new object[]
            {
                new { assetId = "fire", kind = "sigil", x = 0f, y = 0f, scale = 1f, rotation = 0f },
                new { assetId = "water", kind = "sigil", x = 50f, y = 0f, scale = 1f, rotation = 0f },
                new { assetId = "levitation", kind = "sign", x = 0f, y = -120f, scale = 1f, rotation = 0f },
            },
        });
        Assert.Contains("sigil", (await c.ExpectAsync("spellRejected")).GetProperty("reason").GetString());

        await c.SendAsync(new
        {
            type = "uploadSpell", @ref = "y", name = "Huge",
            glyphs = new object[]
            {
                new { assetId = "fire", kind = "sigil", x = 0f, y = 0f, scale = 9f, rotation = 0f },
                new { assetId = "levitation", kind = "sign", x = 0f, y = -120f, scale = 1f, rotation = 0f },
            },
        });
        Assert.Contains("scale", (await c.ExpectAsync("spellRejected")).GetProperty("reason").GetString());

        // Stats the client might claim are ignored; the reply has the real ones
        await c.SendAsync(new { type = "uploadSpell", @ref = "z", name = "Bolt", glyphs = WaterGlyphs, stats = new { power = 999999 } });
        var ok = await c.ExpectAsync("spellAccepted");
        Assert.True(ok.GetProperty("stats").GetProperty("power").GetInt32() < 999999);

        // A layered spell: the server evaluates every part
        await c.SendAsync(new
        {
            type = "uploadSpell", @ref = "l", name = "Layered",
            glyphs = new object[]
            {
                new { assetId = "cooling", kind = "sign", x = 0f, y = -210f, scale = 0.5f, rotation = 0f },
            },
            components = new object[]
            {
                new { source = "a", x = -60f, y = 0f, scale = 0.2f, rotation = 0f, glyphs = WaterGlyphs },
                new { source = "b", x = 60f, y = 0f, scale = 0.2f, rotation = 45f, glyphs = WaterGlyphs },
            },
        });
        var layered = (await c.ExpectAsync("spellAccepted")).GetProperty("stats");
        Assert.Equal((int)SpellKind.Compound, layered.GetProperty("kind").GetInt32());
        Assert.Equal(2, layered.GetProperty("parts").GetArrayLength());

        // Decks can only hold your own spells
        var other = await TestClient.ConnectAsync(server, "b");
        await other.SendAsync(new { type = "upsertDeck", @ref = "d", name = "Stolen", spellIds = new[] { ok.GetProperty("spellId").GetInt64(), 0, 0, 0, 0, 0 } });
        Assert.Contains("uploaded", (await other.ExpectAsync("deckRejected")).GetProperty("reason").GetString());
    }

    static async Task<T> WaitForAsync<T>(Func<Task<T>> get, Func<T, bool> done)
    {
        for (int i = 0; i < 50; ++i)
        {
            var value = await get();
            if (done(value))
                return value;
            await Task.Delay(100);
        }
        return await get();
    }
}
