using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Whas.Server.Data;
using Whas.Server.Net;

namespace Whas.Server.Spells;

// Spell uploads and the deck library. Stats always come from the server's
// evaluator; whatever the client thinks its spell does is ignored.
public sealed class SpellService(IDbContextFactory<WhasDb> dbFactory)
{
    public const int DeckSlots = 6;
    public const int MaxDecksPerPlayer = 64;
    public const int MaxSpellsPerPlayer = 512;

    public sealed record UploadResult(SpellCard? Card, string? Error);

    public Task<UploadResult> UploadAsync(long ownerId, string name, List<Glyph> glyphs,
                                          CancellationToken ct = default) =>
        UploadAsync(ownerId, name, glyphs, [], ct);

    public async Task<UploadResult> UploadAsync(long ownerId, string name, List<Glyph> glyphs,
                                                List<Component> components,
                                                CancellationToken ct = default)
    {
        if (SpellValidator.Check(name, glyphs, components) is { } problem)
            return new(null, problem);

        string glyphsJson = JsonSerializer.Serialize(glyphs, Protocol.Json);
        // Plain spells hash their glyphs alone, as before layered spells
        string? componentsJson = components.Count == 0
            ? null : JsonSerializer.Serialize(components, Protocol.Json);
        string hash = Convert.ToHexString(SHA256.HashData(
            Encoding.UTF8.GetBytes(glyphsJson + (componentsJson ?? ""))));
        var stats = SpellEvaluator.EvaluateQuantized(glyphs, components);
        var cardComponents = components.Count == 0 ? null : components;

        await using var db = await dbFactory.CreateDbContextAsync(ct);
        var existing = await db.Spells.FirstOrDefaultAsync(
            s => s.OwnerId == ownerId && s.GlyphsHash == hash &&
                 s.EvaluatorVersion == SpellEvaluator.Version, ct);
        if (existing is not null)
        {
            // Same circle uploaded again (maybe renamed): reuse its id
            existing.Name = name;
            await db.SaveChangesAsync(ct);
            return new(new SpellCard(existing.Id, name, glyphs, stats, cardComponents), null);
        }

        if (await db.Spells.CountAsync(s => s.OwnerId == ownerId, ct) >= MaxSpellsPerPlayer)
            return new(null, "spell limit reached");

        var row = new SpellDefinition
        {
            OwnerId = ownerId,
            Name = name,
            GlyphsJson = glyphsJson,
            ComponentsJson = componentsJson,
            StatsJson = JsonSerializer.Serialize(stats, Protocol.Json),
            GlyphsHash = hash,
            EvaluatorVersion = SpellEvaluator.Version,
            CreatedAt = DateTimeOffset.UtcNow,
        };
        db.Spells.Add(row);
        await db.SaveChangesAsync(ct);
        return new(new SpellCard(row.Id, name, glyphs, stats, cardComponents), null);
    }

    public async Task<List<SpellCard>> MineAsync(long ownerId, CancellationToken ct = default)
    {
        await using var db = await dbFactory.CreateDbContextAsync(ct);
        var rows = await db.Spells.AsNoTracking().Where(s => s.OwnerId == ownerId)
                               .OrderBy(s => s.Id).ToListAsync(ct);
        return rows.Select(ToCard).ToList();
    }

    public async Task<(long DeckId, string? Error)> UpsertDeckAsync(
        long ownerId, long? deckId, string name, long[] spellIds, CancellationToken ct = default)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Length > 32)
            return (0, "deck name must be 1-32 characters");
        if (spellIds.Length != DeckSlots)
            return (0, $"a deck has {DeckSlots} slots");

        await using var db = await dbFactory.CreateDbContextAsync(ct);
        var wanted = spellIds.Where(id => id != 0).Distinct().ToList();
        int owned = await db.Spells.CountAsync(s => s.OwnerId == ownerId && wanted.Contains(s.Id), ct);
        if (owned != wanted.Count)
            return (0, "deck uses spells you haven't uploaded");

        Deck? deck = null;
        if (deckId is { } id)
        {
            deck = await db.Decks.FirstOrDefaultAsync(d => d.Id == id && d.OwnerId == ownerId, ct);
            if (deck is null)
                return (0, "no such deck");
        }
        else
        {
            if (await db.Decks.CountAsync(d => d.OwnerId == ownerId, ct) >= MaxDecksPerPlayer)
                return (0, "deck limit reached");
            deck = new Deck { OwnerId = ownerId, Name = name };
            db.Decks.Add(deck);
        }
        deck.Name = name;
        deck.SpellIds = spellIds.ToArray();
        deck.UpdatedAt = DateTimeOffset.UtcNow;
        await db.SaveChangesAsync(ct);
        return (deck.Id, null);
    }

    public async Task<bool> DeleteDeckAsync(long ownerId, long deckId, CancellationToken ct = default)
    {
        await using var db = await dbFactory.CreateDbContextAsync(ct);
        int n = await db.Decks.Where(d => d.Id == deckId && d.OwnerId == ownerId)
                              .ExecuteDeleteAsync(ct);
        return n > 0;
    }

    public async Task<List<Deck>> DecksAsync(long ownerId, CancellationToken ct = default)
    {
        await using var db = await dbFactory.CreateDbContextAsync(ct);
        return await db.Decks.AsNoTracking().Where(d => d.OwnerId == ownerId)
                             .OrderBy(d => d.Id).ToListAsync(ct);
    }

    // The cards of the player's decks for rounds 1-3, each six long (null =
    // empty slot). Stats are re-evaluated if the evaluator changed since the
    // upload, so every match plays by the current rules.
    public async Task<(List<SpellCard?[]>? Rounds, string? Error)> LoadRoundDecksAsync(
        long ownerId, long[] deckIds, CancellationToken ct = default)
    {
        if (deckIds.Length != 3)
            return (null, "pick a deck for each of the 3 rounds");
        await using var db = await dbFactory.CreateDbContextAsync(ct);
        var ids = deckIds.Distinct().ToList();
        var decks = await db.Decks.AsNoTracking()
                                  .Where(d => d.OwnerId == ownerId && ids.Contains(d.Id))
                                  .ToDictionaryAsync(d => d.Id, ct);
        if (decks.Count != ids.Count)
            return (null, "unknown deck");

        var spellIds = decks.Values.SelectMany(d => d.SpellIds).Where(id => id != 0)
                            .Distinct().ToList();
        var spells = await db.Spells.Where(s => s.OwnerId == ownerId && spellIds.Contains(s.Id))
                                    .ToDictionaryAsync(s => s.Id, ct);
        foreach (var s in spells.Values.Where(s => s.EvaluatorVersion != SpellEvaluator.Version))
        {
            var glyphs = JsonSerializer.Deserialize<List<Glyph>>(s.GlyphsJson, Protocol.Json)!;
            s.StatsJson = JsonSerializer.Serialize(
                SpellEvaluator.EvaluateQuantized(glyphs, ComponentsOf(s) ?? []), Protocol.Json);
            s.EvaluatorVersion = SpellEvaluator.Version;
        }
        await db.SaveChangesAsync(ct);

        var rounds = new List<SpellCard?[]>();
        foreach (long deckId in deckIds)
        {
            var deck = decks[deckId];
            var cards = new SpellCard?[DeckSlots];
            for (int i = 0; i < DeckSlots && i < deck.SpellIds.Length; ++i)
                if (spells.TryGetValue(deck.SpellIds[i], out var s))
                    cards[i] = ToCard(s);
            if (cards.All(c => c is null))
                return (null, $"deck \"{deck.Name}\" is empty");
            rounds.Add(cards);
        }
        return (rounds, null);
    }

    static List<Component>? ComponentsOf(SpellDefinition s) =>
        s.ComponentsJson is null
            ? null : JsonSerializer.Deserialize<List<Component>>(s.ComponentsJson, Protocol.Json);

    static SpellCard ToCard(SpellDefinition s) => new(
        s.Id, s.Name,
        JsonSerializer.Deserialize<List<Glyph>>(s.GlyphsJson, Protocol.Json)!,
        JsonSerializer.Deserialize<QuantizedStats>(s.StatsJson, Protocol.Json)!,
        ComponentsOf(s));
}
