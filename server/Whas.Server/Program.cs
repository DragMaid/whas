using Microsoft.EntityFrameworkCore;
using Whas.Server.Data;
using Whas.Server.Matches;
using Whas.Server.Net;
using Whas.Server.Players;
using Whas.Server.Spells;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddDbContextFactory<WhasDb>(o =>
    o.UseNpgsql(builder.Configuration.GetConnectionString("Whas")));
builder.Services.Configure<MatchOptions>(builder.Configuration.GetSection("Match"));
builder.Services.AddSingleton<PlayerService>();
builder.Services.AddSingleton<SpellService>();
builder.Services.AddSingleton<MatchmakingService>();
builder.Services.AddSingleton<SessionHandler>();

var app = builder.Build();

await using (var db = await app.Services.GetRequiredService<IDbContextFactory<WhasDb>>()
                                        .CreateDbContextAsync())
    await db.Database.MigrateAsync();

app.UseWebSockets(new WebSocketOptions { KeepAliveInterval = TimeSpan.FromSeconds(20) });

app.MapGet("/health", (MatchmakingService mm) => Results.Ok(new { ok = true, matches = mm.ActiveMatches }));

// The game's connection: one WebSocket per client, JSON messages
app.Map("/ws", async (HttpContext http, SessionHandler handler, MatchmakingService mm,
                      ILogger<ClientSession> log) =>
{
    if (!http.WebSockets.IsWebSocketRequest)
    {
        http.Response.StatusCode = StatusCodes.Status400BadRequest;
        return;
    }
    using var socket = await http.WebSockets.AcceptWebSocketAsync();
    var session = new ClientSession(socket, log);
    try
    {
        await session.RunAsync(handler.HandleAsync, http.RequestAborted);
    }
    finally
    {
        mm.SessionClosed(session);
    }
});

// Read-only views for the client's history, replay and library screens
var api = app.MapGroup("/api").AddEndpointFilter(async (ctx, next) =>
{
    var player = await ctx.HttpContext.RequestServices.GetRequiredService<PlayerService>()
                          .FromRequestAsync(ctx.HttpContext.Request);
    if (player is null)
        return Results.Unauthorized();
    ctx.HttpContext.Items["player"] = player;
    return await next(ctx);
});

static Player Me(HttpContext http) => (Player)http.Items["player"]!;

api.MapGet("/players/me", (HttpContext http) =>
{
    var me = Me(http);
    return Results.Ok(new { playerId = me.Id, wins = me.Wins, losses = me.Losses });
});

api.MapGet("/players/me/matches", async (HttpContext http, IDbContextFactory<WhasDb> dbf) =>
{
    long me = Me(http).Id;
    await using var db = await dbf.CreateDbContextAsync();
    var rows = await db.Matches.AsNoTracking()
        .Where(m => m.Players.Any(p => p.PlayerId == me))
        .OrderByDescending(m => m.StartedAt).Take(50)
        .Select(m => new
        {
            matchId = m.Id,
            startedAt = m.StartedAt,
            endedAt = m.EndedAt,
            status = m.Status.ToString(),
            mode = m.Mode.ToString(),
            won = m.WinnerId == me,
            draw = m.WinnerId == null && m.Status == MatchStatus.Finished,
            opponentId = m.Players.Where(p => p.PlayerId != me).Select(p => p.PlayerId).FirstOrDefault(),
            roundsWon = m.Players.OrderBy(p => p.Slot).Select(p => p.RoundsWon).ToList(),
            slot = m.Players.Where(p => p.PlayerId == me).Select(p => p.Slot).FirstOrDefault(),
        })
        .ToListAsync();
    return Results.Ok(rows);
});

// Everything needed to re-simulate a match: seed, build, decks and plans
api.MapGet("/matches/{id:long}/replay", async (long id, IDbContextFactory<WhasDb> dbf) =>
{
    await using var db = await dbf.CreateDbContextAsync();
    var match = await db.Matches.AsNoTracking().Include(m => m.Players)
                                .FirstOrDefaultAsync(m => m.Id == id);
    if (match is null)
        return Results.NotFound();
    var turns = await db.Turns.AsNoTracking().Where(t => t.MatchId == id)
                              .OrderBy(t => t.Round).ThenBy(t => t.Turn).ToListAsync();
    return Results.Ok(new
    {
        matchId = match.Id,
        seed = Protocol.U64(match.Seed),
        buildId = match.BuildId,
        rulesetVersion = match.RulesetVersion,
        status = match.Status.ToString(),
        players = match.Players.OrderBy(p => p.Slot).Select(p => new
        {
            slot = p.Slot,
            playerId = p.PlayerId,
            roundsWon = p.RoundsWon,
            decks = System.Text.Json.JsonDocument.Parse(p.RoundDecksJson).RootElement,
        }),
        turns = turns.Select(t => new
        {
            round = t.Round,
            turn = t.Turn,
            plans = new[] { t.PlanSlot0Json, t.PlanSlot1Json },
            hashes = new[] { t.HashSlot0, t.HashSlot1 },
        }),
    });
});

api.MapGet("/spells/mine", async (HttpContext http, SpellService spells) =>
    Results.Ok(await spells.MineAsync(Me(http).Id)));

api.MapGet("/decks/mine", async (HttpContext http, SpellService spells) =>
    Results.Ok((await spells.DecksAsync(Me(http).Id))
               .Select(d => new { deckId = d.Id, name = d.Name, spellIds = d.SpellIds })));

app.Run();

public partial class Program;
