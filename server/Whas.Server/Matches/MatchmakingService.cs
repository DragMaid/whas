using System.Collections.Concurrent;
using System.Security.Cryptography;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Whas.Server.Data;
using Whas.Server.Net;
using Whas.Server.Spells;

namespace Whas.Server.Matches;

// Pairs players (quick-match queue or private lobby codes) and keeps the
// running matches. Only clients with the same build id meet: lockstep needs
// both to compute bit-identical floats.
public sealed class MatchmakingService(
    IDbContextFactory<WhasDb> dbFactory, SpellService spells, IOptions<MatchOptions> options,
    ILogger<MatchmakingService> log, IHostApplicationLifetime lifetime)
{
    const string CodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const int CodeLength = 6;

    readonly object _lock = new();
    readonly List<ClientSession> _queue = [];
    readonly Dictionary<string, (ClientSession Host, RoomOptions Options)> _lobbies = [];
    readonly ConcurrentDictionary<long, MatchActor> _matches = new();

    public int ActiveMatches => _matches.Count;

    public async Task QueueAsync(ClientSession session)
    {
        ClientSession? opponent = null;
        lock (_lock)
        {
            if (!CanWait(session))
                return;
            opponent = _queue.FirstOrDefault(o => o.BuildId == session.BuildId &&
                                                  o.Player!.Id != session.Player!.Id && !o.Closed);
            if (opponent is null)
            {
                _queue.Add(session);
                session.Send("queued");
                return;
            }
            _queue.Remove(opponent);
        }
        await StartAsync(opponent, session, MatchMode.Queue, RoomOptions.Default);
    }

    public void Cancel(ClientSession session)
    {
        lock (_lock)
        {
            _queue.Remove(session);
            foreach (var code in _lobbies.Where(kv => kv.Value.Host == session).Select(kv => kv.Key).ToList())
                _lobbies.Remove(code);
        }
    }

    public void CreateLobby(ClientSession session, RoomOptions options)
    {
        string code;
        lock (_lock)
        {
            if (!CanWait(session))
                return;
            do
            {
                code = new string(Enumerable.Range(0, CodeLength)
                    .Select(_ => CodeAlphabet[RandomNumberGenerator.GetInt32(CodeAlphabet.Length)])
                    .ToArray());
            } while (_lobbies.ContainsKey(code));
            _lobbies[code] = (session, options);
        }
        session.Send("lobbyCreated", new { code });
    }

    public async Task JoinLobbyAsync(ClientSession session, string code)
    {
        ClientSession host;
        RoomOptions options;
        lock (_lock)
        {
            if (!CanWait(session))
                return;
            code = code.Trim().ToUpperInvariant();
            if (!_lobbies.TryGetValue(code, out var lobby) || lobby.Host.Closed)
            {
                session.Error("no lobby with that code");
                return;
            }
            (host, options) = lobby;
            if (host.Player!.Id == session.Player!.Id)
            {
                session.Error("that's your own lobby");
                return;
            }
            if (host.BuildId != session.BuildId)
            {
                session.Error("the host runs a different game build");
                return;
            }
            _lobbies.Remove(code);
        }
        await StartAsync(host, session, MatchMode.Lobby, options);
    }

    public void Rejoin(ClientSession session, long matchId)
    {
        if (!_matches.TryGetValue(matchId, out var match) ||
            match.SlotOf(session.Player!.Id) is var slot && slot < 0)
        {
            session.Error("no such match to rejoin");
            return;
        }
        match.Post(new SlotRejoined(slot, session));
    }

    // The match (if any) a player is still part of, for "you have a match in
    // progress" on connect
    public long? RunningMatchOf(long playerId) =>
        _matches.Values.FirstOrDefault(m => m.SlotOf(playerId) >= 0)?.MatchId;

    public void SessionClosed(ClientSession session)
    {
        Cancel(session);
        if (session.Match is { } match && session.Slot >= 0)
            match.Post(new SlotLeft(session.Slot, session));
    }

    bool CanWait(ClientSession session)
    {
        if (session.Match is not null || RunningMatchOf(session.Player!.Id) is not null)
        {
            session.Error("finish (or rejoin) your current match first");
            return false;
        }
        if (_queue.Contains(session) || _lobbies.Values.Any(l => l.Host == session))
        {
            session.Error("already waiting for a match");
            return false;
        }
        return true;
    }

    async Task StartAsync(ClientSession a, ClientSession b, MatchMode mode, RoomOptions room)
    {
        var match = new MatchActor(a, b, mode, room, dbFactory, spells, options.Value, log);
        await match.StartAsync(lifetime.ApplicationStopping);
        _matches[match.MatchId] = match;
        log.LogInformation("match {Match} started ({Mode}) players {A} vs {B}",
                           match.MatchId, mode, a.Player!.Id, b.Player!.Id);
        _ = match.Completion.ContinueWith(done => _matches.TryRemove(match.MatchId, out var _),
                                          TaskScheduler.Default);
    }
}
