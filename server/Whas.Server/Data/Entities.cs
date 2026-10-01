namespace Whas.Server.Data;

public sealed class Player
{
    public long Id { get; set; }
    // SHA-256 of the guest token; the token itself is never stored
    public required string TokenHash { get; set; }
    public DateTimeOffset CreatedAt { get; set; }
    public int Wins { get; set; }
    public int Losses { get; set; }
}

// A spell as uploaded, with the stats the server computed for it
public sealed class SpellDefinition
{
    public long Id { get; set; }
    public long OwnerId { get; set; }
    public required string Name { get; set; }
    public required string GlyphsJson { get; set; }  // jsonb
    // Layered spells: the embedded spells (jsonb); null for plain spells
    public string? ComponentsJson { get; set; }
    public required string StatsJson { get; set; }   // jsonb, quantized
    // Same owner + same glyphs reuses the row
    public required string GlyphsHash { get; set; }
    public int EvaluatorVersion { get; set; }
    public DateTimeOffset CreatedAt { get; set; }
}

public sealed class Deck
{
    public long Id { get; set; }
    public long OwnerId { get; set; }
    public required string Name { get; set; }
    public long[] SpellIds { get; set; } = new long[6]; // 0 = empty slot
    public DateTimeOffset UpdatedAt { get; set; }
}

public enum MatchMode { Queue, Lobby }

public enum MatchStatus { Running, Finished, Forfeit, Voided }

public sealed class Match
{
    public long Id { get; set; }
    public long Seed { get; set; }
    public MatchMode Mode { get; set; }
    public int RulesetVersion { get; set; }
    public required string BuildId { get; set; }
    // The room's maps and modes (RoomOptions), as both clients received them
    public string OptionsJson { get; set; } = "{}"; // jsonb
    public DateTimeOffset StartedAt { get; set; }
    public DateTimeOffset? EndedAt { get; set; }
    public long? WinnerId { get; set; }
    public MatchStatus Status { get; set; }
    public List<MatchPlayer> Players { get; set; } = [];
}

public sealed class MatchPlayer
{
    public long MatchId { get; set; }
    public long PlayerId { get; set; }
    public int Slot { get; set; }
    // Per round: the six spells (id, name, glyphs, stats) locked at start
    public required string RoundDecksJson { get; set; } // jsonb
    public int RoundsWon { get; set; }
}

// One executed turn: both accepted plans and each client's state hash after
// it. Seed + these rows re-simulate the whole match.
public sealed class TurnRecord
{
    public long MatchId { get; set; }
    public int Round { get; set; }
    public int Turn { get; set; }
    public required string PlanSlot0Json { get; set; } // jsonb
    public required string PlanSlot1Json { get; set; } // jsonb
    public string? HashSlot0 { get; set; }
    public string? HashSlot1 { get; set; }
    public DateTimeOffset At { get; set; }
}

public sealed class SpellUsage
{
    public long SpellId { get; set; }
    public long MatchId { get; set; }
    public int Casts { get; set; }
}

public sealed class DesyncReport
{
    public long Id { get; set; }
    public long MatchId { get; set; }
    public int Round { get; set; }
    public int Turn { get; set; }
    public required string HashA { get; set; }
    public required string HashB { get; set; }
    public int ReferenceSlot { get; set; }
    public string? BuildIdA { get; set; }
    public string? BuildIdB { get; set; }
    // The non-reference player; repeated reports flag them for review
    public long SuspectPlayerId { get; set; }
    public DateTimeOffset At { get; set; }
}
