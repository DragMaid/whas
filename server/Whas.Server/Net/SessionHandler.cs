using System.Text.Json;
using Whas.Server.Matches;
using Whas.Server.Players;
using Whas.Server.Spells;

namespace Whas.Server.Net;

// Routes one client's messages: account and library requests are answered
// here, match traffic goes to the player's MatchActor.
public sealed class SessionHandler(PlayerService players, SpellService spells,
                                   MatchmakingService matchmaking)
{
    static readonly HashSet<string> MatchTypes =
        ["matchDecks", "commit", "reveal", "stateHash", "snapshot", "leave"];

    public async Task HandleAsync(ClientSession session, JsonElement msg)
    {
        string type = msg.Str("type");
        if (type == "hello")
        {
            await HelloAsync(session, msg);
            return;
        }
        if (session.Player is null)
        {
            session.Error("say hello first");
            return;
        }
        long me = session.Player.Id;

        if (MatchTypes.Contains(type))
        {
            if (session.Match is { } match && session.Slot >= 0)
                match.Post(new MatchMessage(session.Slot, type, msg.Clone()));
            else
                session.Error("not in a match");
            return;
        }

        switch (type)
        {
            case "uploadSpell":
            {
                string clientRef = msg.OptStr("ref") ?? "";
                var result = await spells.UploadAsync(me, msg.Str("name"), msg.Glyphs(),
                                                       msg.Components());
                if (result.Card is { } card)
                    session.Send("spellAccepted", new { @ref = clientRef, spellId = card.Id, stats = card.Stats });
                else
                    session.Send("spellRejected", new { @ref = clientRef, reason = result.Error });
                break;
            }
            case "listSpells":
                session.Send("spells", new { spells = await spells.MineAsync(me) });
                break;
            case "upsertDeck":
            {
                string clientRef = msg.OptStr("ref") ?? "";
                long? deckId = msg.TryGetProperty("deckId", out var d) && d.ValueKind == JsonValueKind.Number
                                   ? d.GetInt64() : null;
                long[] ids = msg.GetProperty("spellIds").Deserialize<long[]>() ?? [];
                var (id, error) = await spells.UpsertDeckAsync(me, deckId, msg.Str("name"), ids);
                if (error is null)
                    session.Send("deckAccepted", new { @ref = clientRef, deckId = id });
                else
                    session.Send("deckRejected", new { @ref = clientRef, reason = error });
                break;
            }
            case "deleteDeck":
                await spells.DeleteDeckAsync(me, msg.Long("deckId"));
                break;
            case "listDecks":
                session.Send("decks", new
                {
                    decks = (await spells.DecksAsync(me)).Select(x => new { deckId = x.Id, name = x.Name, spellIds = x.SpellIds }),
                });
                break;
            case "queue":
                await matchmaking.QueueAsync(session);
                break;
            case "cancelQueue":
                matchmaking.Cancel(session);
                session.Send("queueCancelled");
                break;
            case "createLobby":
                if (RoomOptions.TryParse(msg, out var room, out var roomError))
                    matchmaking.CreateLobby(session, room);
                else
                    session.Error(roomError);
                break;
            case "joinLobby":
                await matchmaking.JoinLobbyAsync(session, msg.Str("code"));
                break;
            case "rejoin":
                matchmaking.Rejoin(session, msg.Long("matchId"));
                break;
            default:
                session.Error($"unknown message type '{type}'");
                break;
        }
    }

    async Task HelloAsync(ClientSession session, JsonElement msg)
    {
        if (session.Player is not null)
        {
            session.Error("already said hello");
            return;
        }
        if (msg.TryGetProperty("protocol", out var p) && p.ValueKind == JsonValueKind.Number &&
            p.GetInt32() != Protocol.Version)
        {
            session.Error($"protocol {p.GetInt32()} is not supported (server speaks {Protocol.Version})");
            return;
        }
        session.BuildId = msg.Str("buildId");
        string? token = msg.OptStr("token");
        var player = await players.FindAsync(token);
        string? newToken = null;
        if (player is null)
            (player, newToken) = await players.CreateGuestAsync();
        session.Player = player;
        session.Send("welcome", new
        {
            playerId = player.Id,
            token = newToken, // only when a new guest was made; keep it
            protocol = Protocol.Version,
            runningMatch = matchmaking.RunningMatchOf(player.Id),
        });
    }
}
