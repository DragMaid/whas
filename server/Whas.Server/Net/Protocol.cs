using System.Text.Json;
using System.Text.Json.Serialization;
using Whas.Server.Spells;

namespace Whas.Server.Net;

// Wire format shared with the C++ client; see docs/protocol.md
public static class Protocol
{
    public const int Version = 1;
    public const int RulesetVersion = 1;

    public static readonly JsonSerializerOptions Json = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
    };

    public static string Str(this JsonElement e, string name) =>
        e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.String
            ? p.GetString()! : throw new FormatException($"missing string '{name}'");

    public static string? OptStr(this JsonElement e, string name) =>
        e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.String ? p.GetString() : null;

    public static int Int(this JsonElement e, string name) =>
        e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.Number
            ? p.GetInt32() : throw new FormatException($"missing number '{name}'");

    public static long Long(this JsonElement e, string name) =>
        e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.Number
            ? p.GetInt64() : throw new FormatException($"missing number '{name}'");

    public static List<Glyph> Glyphs(this JsonElement e) =>
        e.GetProperty("glyphs").Deserialize<List<Glyph>>(Json)
        ?? throw new FormatException("missing glyphs");

    // 64-bit values (seeds, state hashes) travel as strings: JSON numbers
    // lose precision past 2^53 in many parsers
    public static string U64(ulong v) => v.ToString();
    public static string U64(long v) => unchecked((ulong)v).ToString();
}

// A spell as a match hands it to both clients: enough to draw it and
// simulate it without evaluating anything
public sealed record SpellCard(
    [property: JsonPropertyName("id")] long Id,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("glyphs")] List<Glyph> Glyphs,
    [property: JsonPropertyName("stats")] QuantizedStats Stats);
