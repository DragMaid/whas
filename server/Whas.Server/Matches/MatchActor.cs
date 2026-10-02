using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;
using Microsoft.EntityFrameworkCore;
using Whas.Server.Data;
using Whas.Server.Net;
using Whas.Server.Spells;

namespace Whas.Server.Matches;

public sealed class MatchOptions
{
    public TimeSpan DeckDeadline { get; set; } = TimeSpan.FromSeconds(60);
    public TimeSpan PlanDeadline { get; set; } = TimeSpan.FromSeconds(30);
    public TimeSpan RevealDeadline { get; set; } = TimeSpan.FromSeconds(10);
    public TimeSpan HashDeadline { get; set; } = TimeSpan.FromSeconds(30);
    public TimeSpan SnapshotDeadline { get; set; } = TimeSpan.FromSeconds(15);
    public TimeSpan RejoinGrace { get; set; } = TimeSpan.FromSeconds(60);
    // A round that runs this long without a knockout is a draw
    public int MaxTurnsPerRound { get; set; } = 40;
    // Real time: how long a missing input batch is waited for before the
    // player is taken to stand still for it
    public TimeSpan RtsInputTimeout { get; set; } = TimeSpan.FromSeconds(2);
}

public abstract record MatchEvent;
// Body is cloned, so it outlives the socket's JsonDocument
public sealed record MatchMessage(int Slot, string Type, JsonElement Body) : MatchEvent;
public sealed record SlotLeft(int Slot, ClientSession Session) : MatchEvent;
public sealed record SlotRejoined(int Slot, ClientSession Session) : MatchEvent;

// Runs one match: lock decks, then per turn collect committed plans, reveal
// and validate them, send both plans to both clients, and compare the state
// hashes the clients report after simulating. The server never simulates;
// it relays, validates and referees on agreed results. Every event arrives
// on one channel, so the match state is only ever touched by this loop.
public sealed class MatchActor
{
    public const int Players = 2;
    public const int Rounds = 3;

    sealed class MatchOver(int winner, MatchStatus status, string reason) : Exception(reason)
    {
        public int Winner { get; } = winner; // slot, -1 draw/void
        public MatchStatus Status { get; } = status;
    }

    sealed record TurnHistory(int Round, int Turn, string[] Plans);

    readonly Channel<MatchEvent> _events = Channel.CreateUnbounded<MatchEvent>();
    readonly IDbContextFactory<WhasDb> _dbFactory;
    readonly SpellService _spells;
    readonly MatchOptions _options;
    readonly ILogger _log;
    readonly TimeProvider _time;

    readonly ClientSession?[] _sessions = new ClientSession?[Players];
    readonly long[] _playerIds = new long[Players];
    readonly string _buildId;
    readonly DateTimeOffset?[] _graceUntil = new DateTimeOffset?[Players];
    readonly List<SpellCard?[]>?[] _decks = new List<SpellCard?[]>?[Players];
    readonly int[] _roundsWon = new int[Players];
    readonly List<TurnHistory> _history = [];
    readonly Dictionary<long, int> _casts = [];

    // Where the match is, for clients rejoining mid-turn
    int _round, _turn;
    string _phase = "decks";
    DateTimeOffset _phaseDeadline;
    readonly bool[] _committed = new bool[Players];

    public long MatchId { get; private set; }
    public ulong Seed { get; }
    public MatchMode Mode { get; }
    public RoomOptions Room { get; }
    public Task Completion { get; private set; } = Task.CompletedTask;

    public MatchActor(ClientSession a, ClientSession b, MatchMode mode, RoomOptions room,
                      IDbContextFactory<WhasDb> dbFactory, SpellService spells,
                      MatchOptions options, ILogger log, TimeProvider? time = null)
    {
        _dbFactory = dbFactory;
        _spells = spells;
        _options = options;
        _log = log;
        _time = time ?? TimeProvider.System;
        Mode = mode;
        Room = room;
        Seed = BitConverter.ToUInt64(RandomNumberGenerator.GetBytes(8));
        _buildId = a.BuildId ?? "";
        // Who is slot 0 is random too
        bool swap = RandomNumberGenerator.GetInt32(2) == 1;
        _sessions[0] = swap ? b : a;
        _sessions[1] = swap ? a : b;
        for (int i = 0; i < Players; ++i)
            _playerIds[i] = _sessions[i]!.Player!.Id;
    }

    public int SlotOf(long playerId) => Array.IndexOf(_playerIds, playerId);

    public void Post(MatchEvent e) => _events.Writer.TryWrite(e);

    DateTimeOffset Now => _time.GetUtcNow();

    public async Task StartAsync(CancellationToken ct)
    {
        await using (var db = await _dbFactory.CreateDbContextAsync(ct))
        {
            var match = new Match
            {
                Seed = unchecked((long)Seed),
                Mode = Mode,
                RulesetVersion = Protocol.RulesetVersion,
                BuildId = _buildId,
                OptionsJson = Room.Json.GetRawText(),
                StartedAt = Now,
                Status = MatchStatus.Running,
            };
            db.Matches.Add(match);
            await db.SaveChangesAsync(ct);
            MatchId = match.Id;
        }
        for (int i = 0; i < Players; ++i)
        {
            _sessions[i]!.Match = this;
            _sessions[i]!.Slot = i;
        }
        Completion = Task.Run(() => RunAsync(ct), ct);
    }

    async Task RunAsync(CancellationToken ct)
    {
        int winner = -1;
        var status = MatchStatus.Finished;
        string reason = "rounds";
        try
        {
            await DeckPhaseAsync(ct);
            winner = await PlayRoundsAsync(ct);
        }
        catch (MatchOver over)
        {
            winner = over.Winner;
            status = over.Status;
            reason = over.Message;
        }
        catch (OperationCanceledException)
        {
            status = MatchStatus.Voided;
            reason = "server shutting down";
        }
        catch (Exception e)
        {
            _log.LogError(e, "match {Match} failed", MatchId);
            status = MatchStatus.Voided;
            reason = "server error";
        }

        // Stored first, so a client fetching the replay on matchEnd gets the
        // finished match
        try
        {
            await PersistEndAsync(winner, status, CancellationToken.None);
        }
        catch (Exception e)
        {
            _log.LogError(e, "match {Match} result not saved", MatchId);
        }
        Broadcast("matchEnd", new { winner, reason, status = status.ToString(), roundsWon = _roundsWon });
        foreach (var s in _sessions)
            if (s is not null && s.Match == this)
            {
                s.Match = null;
                s.Slot = -1;
            }
    }

    // ---- Events ------------------------------------------------------------

    // Next match message before the deadline (null once it passes). Handles
    // disconnects, rejoins and forfeits on the way.
    async Task<MatchMessage?> NextAsync(DateTimeOffset deadline, CancellationToken ct)
    {
        while (true)
        {
            var now = Now;
            for (int i = 0; i < Players; ++i)
                if (_graceUntil[i] is { } until && now >= until)
                    throw ForfeitBy(i, "did not reconnect in time");
            var wake = deadline;
            foreach (var until in _graceUntil)
                if (until is { } u && u < wake)
                    wake = u;
            if (now >= deadline)
                return null;

            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeout.CancelAfter(wake - now);
            MatchEvent e;
            try { e = await _events.Reader.ReadAsync(timeout.Token); }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested) { continue; }

            switch (e)
            {
                case SlotLeft left when _sessions[left.Slot] == left.Session:
                    _sessions[left.Slot] = null;
                    _graceUntil[left.Slot] = Now + _options.RejoinGrace;
                    SendTo(1 - left.Slot, "opponentDisconnected",
                           new { graceSec = (int)_options.RejoinGrace.TotalSeconds });
                    if (_sessions[1 - left.Slot] is null)
                        throw new MatchOver(-1, MatchStatus.Voided, "both players left");
                    break;
                case SlotRejoined joined:
                    if (_sessions[joined.Slot] is { } old && old != joined.Session)
                        old.Match = null; // a stale socket that hasn't closed yet
                    _sessions[joined.Slot] = joined.Session;
                    _graceUntil[joined.Slot] = null;
                    joined.Session.Match = this;
                    joined.Session.Slot = joined.Slot;
                    SendCatchUp(joined.Slot);
                    SendTo(1 - joined.Slot, "opponentReconnected");
                    break;
                case MatchMessage { Type: "leave" } leave:
                    throw ForfeitBy(leave.Slot, "left the match");
                case MatchMessage msg:
                    return msg;
            }
        }
    }

    MatchOver ForfeitBy(int slot, string why) =>
        new(1 - slot, MatchStatus.Forfeit, $"player {slot} {why}");

    bool Connected(int slot) => _sessions[slot] is not null;

    void SendTo(int slot, string type, object? payload = null) =>
        _sessions[slot]?.Send(type, payload);

    void Broadcast(string type, object? payload = null)
    {
        for (int i = 0; i < Players; ++i)
            SendTo(i, type, payload);
    }

    long DeadlineMs(DateTimeOffset deadline) =>
        Math.Max(0, (long)(deadline - Now).TotalMilliseconds);

    // ---- Decks -------------------------------------------------------------

    async Task DeckPhaseAsync(CancellationToken ct)
    {
        _phase = "decks";
        _phaseDeadline = Now + _options.DeckDeadline;
        for (int i = 0; i < Players; ++i)
            SendTo(i, "matchFound", new
            {
                matchId = MatchId,
                seed = Protocol.U64(Seed),
                slot = i,
                rulesetVersion = Protocol.RulesetVersion,
                mode = Mode.ToString(),
                options = Room.Json,
                deckDeadlineMs = DeadlineMs(_phaseDeadline),
            });

        while (_decks[0] is null || _decks[1] is null)
        {
            var msg = await NextAsync(_phaseDeadline, ct);
            if (msg is null)
            {
                if (_decks[0] is null && _decks[1] is null)
                    throw new MatchOver(-1, MatchStatus.Voided, "nobody picked decks");
                throw ForfeitBy(_decks[0] is null ? 0 : 1, "did not pick decks");
            }
            if (msg.Type != "matchDecks" || _decks[msg.Slot] is not null)
                continue;
            long[] ids = msg.Body.GetProperty("deckIds").Deserialize<long[]>() ?? [];
            var (rounds, error) = await _spells.LoadRoundDecksAsync(_playerIds[msg.Slot], ids,
                                                                    Room.Chaos, ct);
            if (rounds is null)
            {
                SendTo(msg.Slot, "decksRejected", new { reason = error });
                continue;
            }
            _decks[msg.Slot] = rounds;
            SendTo(msg.Slot, "decksLocked", new { rounds });
        }

        await using var db = await _dbFactory.CreateDbContextAsync(ct);
        for (int i = 0; i < Players; ++i)
            db.MatchPlayers.Add(new MatchPlayer
            {
                MatchId = MatchId,
                PlayerId = _playerIds[i],
                Slot = i,
                RoundDecksJson = JsonSerializer.Serialize(_decks[i], Protocol.Json),
            });
        await db.SaveChangesAsync(ct);
    }

    Dictionary<long, QuantizedStats> DeckMap(int slot, int round) =>
        _decks[slot]![round].Where(c => c is not null)
                            .GroupBy(c => c!.Id)
                            .ToDictionary(g => g.Key, g => g.First()!.Stats);

    // ---- Rounds and turns --------------------------------------------------

    async Task<int> PlayRoundsAsync(CancellationToken ct)
    {
        int needed = Rounds / 2 + 1;
        for (_round = 0; _round < Rounds; ++_round)
        {
            Broadcast("roundStart", new
            {
                round = _round,
                decks = new[] { _decks[0]![_round], _decks[1]![_round] },
                roundsWon = _roundsWon,
            });
            var decks = new[] { DeckMap(0, _round), DeckMap(1, _round) };

            int roundWinner = Players; // draw unless someone wins
            if (Room.Rts)
                roundWinner = await PlayRtsRoundAsync(decks, ct);
            else for (_turn = 0; _turn < _options.MaxTurnsPerRound; ++_turn)
            {
                var plans = await PlayTurnAsync(decks, ct);
                int winner = await CheckTurnAsync(plans, ct);
                if (winner >= 0)
                {
                    roundWinner = winner;
                    break;
                }
            }

            if (roundWinner < Players)
                _roundsWon[roundWinner]++;
            Broadcast("roundEnd", new { round = _round, winner = roundWinner, roundsWon = _roundsWon });
            if (_roundsWon[0] >= needed || _roundsWon[1] >= needed)
                break;
        }
        return _roundsWon[0] == _roundsWon[1] ? -1 : _roundsWon[0] > _roundsWon[1] ? 0 : 1;
    }

    static string CommitHash(string plan, string nonce) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(plan + nonce))).ToLowerInvariant();

    async Task<string[]> PlayTurnAsync(Dictionary<long, QuantizedStats>[] decks, CancellationToken ct)
    {
        // Commit: each player sends hash(plan + nonce) so neither can wait
        // for the other's plan and react to it
        _phase = "commit";
        _phaseDeadline = Now + _options.PlanDeadline;
        Array.Clear(_committed);
        var commits = new string?[Players];
        Broadcast("turnStart", new { round = _round, turn = _turn, deadlineMs = DeadlineMs(_phaseDeadline) });
        while (Enumerable.Range(0, Players).Any(i => commits[i] is null && Connected(i)))
        {
            var msg = await NextAsync(_phaseDeadline, ct);
            if (msg is null)
                break;
            if (msg.Type != "commit" || !IsThisTurn(msg.Body) || commits[msg.Slot] is not null)
                continue;
            string hash = msg.Body.Str("hash").ToLowerInvariant();
            if (hash.Length != 64 || !hash.All(Uri.IsHexDigit))
            {
                SendTo(msg.Slot, "error", new { message = "bad commit hash" });
                continue;
            }
            commits[msg.Slot] = hash;
            _committed[msg.Slot] = true;
            SendTo(1 - msg.Slot, "opponentCommitted", new { round = _round, turn = _turn });
        }

        // Reveal: both plans are committed, now they can be shown
        _phase = "reveal";
        _phaseDeadline = Now + _options.RevealDeadline;
        Broadcast("bothCommitted", new { round = _round, turn = _turn, deadlineMs = DeadlineMs(_phaseDeadline) });
        var plans = new string?[Players];
        var reasons = new string?[Players];
        for (int i = 0; i < Players; ++i)
            if (commits[i] is null)
                reasons[i] = "no plan in time";
        while (Enumerable.Range(0, Players).Any(i => commits[i] is not null && plans[i] is null &&
                                                     reasons[i] is null && Connected(i)))
        {
            var msg = await NextAsync(_phaseDeadline, ct);
            if (msg is null)
                break;
            int s = msg.Slot;
            if (msg.Type != "reveal" || !IsThisTurn(msg.Body) || commits[s] is null ||
                plans[s] is not null || reasons[s] is not null)
                continue;
            string plan = msg.Body.Str("plan");
            string nonce = msg.Body.Str("nonce");
            if (CommitHash(plan, nonce) != commits[s])
            {
                reasons[s] = "reveal does not match commit";
                continue;
            }
            var check = PlanValidator.Validate(plan, decks[s]);
            if (!check.Ok)
            {
                reasons[s] = check.Error;
                continue;
            }
            plans[s] = plan;
            foreach (long id in check.CastSpellIds)
                _casts[id] = _casts.GetValueOrDefault(id) + 1;
        }

        var final = new string[Players];
        var substituted = new bool[Players];
        for (int i = 0; i < Players; ++i)
        {
            if (plans[i] is not null)
            {
                final[i] = plans[i]!;
                continue;
            }
            // Missed or rejected: the player stands still this turn
            final[i] = PlanValidator.EmptyPlan;
            substituted[i] = true;
            string why = reasons[i] ?? "no reveal in time";
            _log.LogInformation("match {Match} r{Round} t{Turn}: slot {Slot} plan replaced ({Why})",
                                MatchId, _round, _turn, i, why);
            SendTo(i, "planRejected", new { round = _round, turn = _turn, reason = why });
        }

        Broadcast("turnPlans", new { round = _round, turn = _turn, plans = final, substituted });
        _history.Add(new TurnHistory(_round, _turn, final));
        await using var db = await _dbFactory.CreateDbContextAsync(ct);
        db.Turns.Add(new TurnRecord
        {
            MatchId = MatchId,
            Round = _round,
            Turn = _turn,
            PlanSlot0Json = final[0],
            PlanSlot1Json = final[1],
            At = Now,
        });
        await db.SaveChangesAsync(ct);
        return final;
    }

    // ---- Real time -----------------------------------------------------------

    // Real time (rts.h): both players stream their input in batches of
    // BatchTicks ticks. A batch is relayed as soon as both players' parts are
    // in (or a missing one has waited RtsInputTimeout, or its player is gone).
    // Clients report a state hash every few batches, and as soon as someone
    // falls; agreeing reports with a winner end the round. Returns the round
    // winner, Players for a draw.
    public const int RtsRoundBatches = 1800; // 3 minutes (Rts::ROUND_BATCHES)
    const int RtsMaxAhead = 50;              // batches a client may send early
    const int MaxPlanLength = 16 * 1024;

    async Task<int> PlayRtsRoundAsync(Dictionary<long, QuantizedStats>[] decks, CancellationToken ct)
    {
        const int batchTicks = PlanValidator.BatchTicks;
        _phase = "rts";
        _turn = 0;
        var readyAt = new[] { new Dictionary<long, int>(), new Dictionary<long, int>() };
        var pending = new Dictionary<int, string?[]>();
        var reports = new Dictionary<int, (string? Hash, int Winner)[]>();
        var unsaved = new Dictionary<int, TurnRecord>();
        var waitingSince = Now;

        string?[] Inputs(int batch) =>
            pending.TryGetValue(batch, out var got) ? got : pending[batch] = new string?[Players];

        async Task FlushAsync()
        {
            if (unsaved.Count == 0)
                return;
            await using var db = await _dbFactory.CreateDbContextAsync(ct);
            db.Turns.AddRange(unsaved.Values);
            await db.SaveChangesAsync(ct);
            unsaved.Clear();
        }

        while (_turn < RtsRoundBatches)
        {
            var got = Inputs(_turn);
            bool timedOut = Now >= waitingSince + _options.RtsInputTimeout;
            if (Enumerable.Range(0, Players).All(i => got[i] is not null || !Connected(i) || timedOut))
            {
                var final = new string[Players];
                var substituted = new bool[Players];
                for (int i = 0; i < Players; ++i)
                {
                    string? why = got[i] is null ? "no input in time" : null;
                    if (got[i] is { } plan)
                    {
                        var check = PlanValidator.ValidateBatch(plan, decks[i], _turn * batchTicks,
                                                                readyAt[i]);
                        if (check.Ok)
                        {
                            final[i] = plan;
                            foreach (long id in check.CastSpellIds)
                                _casts[id] = _casts.GetValueOrDefault(id) + 1;
                            continue;
                        }
                        why = check.Error;
                        SendTo(i, "planRejected", new { round = _round, turn = _turn, reason = why });
                    }
                    final[i] = PlanValidator.EmptyPlan;
                    substituted[i] = true;
                }
                Broadcast("frames", new { round = _round, batch = _turn, plans = final, substituted });
                _history.Add(new TurnHistory(_round, _turn, final));
                unsaved[_turn] = new TurnRecord
                {
                    MatchId = MatchId,
                    Round = _round,
                    Turn = _turn,
                    PlanSlot0Json = final[0],
                    PlanSlot1Json = final[1],
                    At = Now,
                };
                pending.Remove(_turn);
                ++_turn;
                waitingSince = Now;
                continue;
            }

            var msg = await NextAsync(waitingSince + _options.RtsInputTimeout, ct);
            if (msg is null || msg.Body.ValueKind != JsonValueKind.Object ||
                !msg.Body.TryGetProperty("round", out var r) || r.ValueKind != JsonValueKind.Number ||
                r.GetInt32() != _round)
                continue;

            if (msg.Type == "inputs")
            {
                int batch = msg.Body.Int("batch");
                string plan = msg.Body.Str("plan");
                if (batch >= _turn && batch < _turn + RtsMaxAhead && plan.Length <= MaxPlanLength &&
                    Inputs(batch)[msg.Slot] is null)
                    Inputs(batch)[msg.Slot] = plan;
                continue;
            }
            if (msg.Type != "stateHash")
                continue;

            int at = msg.Body.Int("turn");
            if (at < 0 || at >= _turn)
                continue;
            if (!reports.TryGetValue(at, out var rep))
                reports[at] = rep = new (string?, int)[Players];
            if (rep[msg.Slot].Hash is not null)
                continue;
            rep[msg.Slot] = (msg.Body.Str("hash"), Math.Clamp(msg.Body.Int("winner"), -1, Players));
            if (unsaved.TryGetValue(at, out var record))
            {
                if (msg.Slot == 0) record.HashSlot0 = rep[0].Hash;
                else record.HashSlot1 = rep[1].Hash;
            }

            bool both = rep[0].Hash is not null && rep[1].Hash is not null;
            if (!both && Connected(1 - msg.Slot))
                continue;
            if (both && (rep[0].Hash != rep[1].Hash || rep[0].Winner != rep[1].Winner))
            {
                await FlushAsync();
                await using var db = await _dbFactory.CreateDbContextAsync(ct);
                db.DesyncReports.Add(new DesyncReport
                {
                    MatchId = MatchId,
                    Round = _round,
                    Turn = at,
                    HashA = rep[0].Hash!,
                    HashB = rep[1].Hash!,
                    ReferenceSlot = -1,
                    BuildIdA = _buildId,
                    BuildIdB = _buildId,
                    At = Now,
                });
                await db.SaveChangesAsync(ct);
                throw new MatchOver(-1, MatchStatus.Voided, "the players' worlds drifted apart");
            }
            reports.Remove(at);
            await FlushAsync();
            int winner = rep[both ? 0 : msg.Slot].Winner;
            if (winner >= 0)
                return winner;
        }
        await FlushAsync();
        return Players; // out of time
    }

    bool IsThisTurn(JsonElement body) =>
        body.Int("round") == _round && body.Int("turn") == _turn;

    // After both clients simulate the turn they report their state hash and
    // who (if anyone) won the round. Returns the round winner (-1 = keep
    // playing, 2 = double knockout).
    async Task<int> CheckTurnAsync(string[] plans, CancellationToken ct)
    {
        _phase = "hash";
        _phaseDeadline = Now + _options.HashDeadline;
        var hashes = new string?[Players];
        var winners = new int[Players];
        while (Enumerable.Range(0, Players).Any(i => hashes[i] is null && Connected(i)))
        {
            var msg = await NextAsync(_phaseDeadline, ct);
            if (msg is null)
                break;
            if (msg.Type != "stateHash" || !IsThisTurn(msg.Body) || hashes[msg.Slot] is not null)
                continue;
            hashes[msg.Slot] = msg.Body.Str("hash");
            winners[msg.Slot] = Math.Clamp(msg.Body.Int("winner"), -1, Players);
        }

        await using (var db = await _dbFactory.CreateDbContextAsync(ct))
        {
            await db.Turns.Where(t => t.MatchId == MatchId && t.Round == _round && t.Turn == _turn)
                          .ExecuteUpdateAsync(u => u.SetProperty(t => t.HashSlot0, hashes[0])
                                                    .SetProperty(t => t.HashSlot1, hashes[1]), ct);
        }

        if (hashes[0] is null && hashes[1] is null)
            throw new MatchOver(-1, MatchStatus.Voided, "no state reports");
        if (hashes[0] is null || hashes[1] is null)
            return winners[hashes[0] is null ? 1 : 0]; // only one could report
        if (hashes[0] == hashes[1] && winners[0] == winners[1])
            return winners[0];
        return await ResyncAsync(hashes!, winners, ct);
    }

    // The clients disagree. One of them, picked from the seed so neither can
    // choose, is the reference: its world snapshot overwrites the other's and
    // its result stands. The other player is recorded as the suspect.
    async Task<int> ResyncAsync(string[] hashes, int[] winners, CancellationToken ct)
    {
        int reference = (int)((Seed ^ ((ulong)_round * 31 + (ulong)_turn)) & 1);
        if (!Connected(reference))
            reference = 1 - reference;
        _phase = "resync";
        _phaseDeadline = Now + _options.SnapshotDeadline;
        Broadcast("desync", new { round = _round, turn = _turn, referenceSlot = reference });

        await using (var db = await _dbFactory.CreateDbContextAsync(ct))
        {
            db.DesyncReports.Add(new DesyncReport
            {
                MatchId = MatchId,
                Round = _round,
                Turn = _turn,
                HashA = hashes[0],
                HashB = hashes[1],
                ReferenceSlot = reference,
                BuildIdA = _buildId,
                BuildIdB = _buildId,
                SuspectPlayerId = _playerIds[1 - reference],
                At = Now,
            });
            await db.SaveChangesAsync(ct);
        }

        while (true)
        {
            var msg = await NextAsync(_phaseDeadline, ct);
            if (msg is null)
                throw new MatchOver(-1, MatchStatus.Voided, "desync could not be resolved");
            if (msg.Type != "snapshot" || msg.Slot != reference || !IsThisTurn(msg.Body))
                continue;
            string data = msg.Body.Str("data");
            SendTo(1 - reference, "snapshot", new { round = _round, turn = _turn, data, hash = hashes[reference] });
            return winners[reference];
        }
    }

    // A returning player replays the match from the seed and every plan so
    // far, then continues from the current phase
    void SendCatchUp(int slot)
    {
        SendTo(slot, "catchUp", new
        {
            matchId = MatchId,
            seed = Protocol.U64(Seed),
            slot,
            rulesetVersion = Protocol.RulesetVersion,
            options = Room.Json,
            decks = _decks.Select(d => d?.Take(_round + 1).ToList()).ToArray(),
            yourDecks = _decks[slot],
            turns = _history.Select(h => new { round = h.Round, turn = h.Turn, plans = h.Plans }),
            roundsWon = _roundsWon,
            current = new
            {
                round = _round,
                turn = _turn,
                phase = _phase,
                committed = _committed[slot],
                deadlineMs = DeadlineMs(_phaseDeadline),
            },
        });
    }

    async Task PersistEndAsync(int winner, MatchStatus status, CancellationToken ct)
    {
        try
        {
            await using var db = await _dbFactory.CreateDbContextAsync(ct);
            var match = await db.Matches.FirstAsync(m => m.Id == MatchId, ct);
            match.EndedAt = Now;
            match.Status = status;
            match.WinnerId = winner is 0 or 1 ? _playerIds[winner] : null;

            for (int i = 0; i < Players; ++i)
                await db.MatchPlayers.Where(p => p.MatchId == MatchId && p.Slot == i)
                                     .ExecuteUpdateAsync(u => u.SetProperty(p => p.RoundsWon, _roundsWon[i]), ct);

            if (winner is 0 or 1 && status != MatchStatus.Voided)
            {
                long winnerId = _playerIds[winner], loserId = _playerIds[1 - winner];
                await db.Players.Where(p => p.Id == winnerId)
                                .ExecuteUpdateAsync(u => u.SetProperty(p => p.Wins, p => p.Wins + 1), ct);
                await db.Players.Where(p => p.Id == loserId)
                                .ExecuteUpdateAsync(u => u.SetProperty(p => p.Losses, p => p.Losses + 1), ct);
            }
            foreach (var (spellId, casts) in _casts)
                db.SpellUsage.Add(new SpellUsage { SpellId = spellId, MatchId = MatchId, Casts = casts });
            await db.SaveChangesAsync(ct);
        }
        catch (Exception e)
        {
            _log.LogError(e, "could not record the end of match {Match}", MatchId);
        }
    }
}
