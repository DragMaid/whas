using System.Text.Json.Serialization;

namespace Whas.Server.Spells;

// Mirrors the C++ enums (include/whas/core/element.h, spell_system.h); the
// numbers go over the wire.
public enum Element : byte
{
    Air = 0, Water = 1, Earth = 2, Fire = 3, Steam = 4, Cloud = 5, Ice = 6,
    Sand = 7, Rock = 8,
}

public enum SpellKind : byte { None = 0, Element = 1, Flight = 2, Gust = 3 }

// A glyph as the client's spell files and editor store it
public sealed record Glyph(
    [property: JsonPropertyName("assetId")] string AssetId,
    [property: JsonPropertyName("kind")] string Kind,
    [property: JsonPropertyName("x")] float X,
    [property: JsonPropertyName("y")] float Y,
    [property: JsonPropertyName("scale")] float Scale,
    [property: JsonPropertyName("rotation")] float Rotation)
{
    public bool IsSigil => Kind == "sigil";
}

// Float stats, the same fields as the C++ SpellStats
public sealed class SpellStats
{
    public bool Valid;
    public SpellKind Kind;
    public Element Element;
    public float NetX, NetY;
    public float TotalMagnitude;
    public float Imbalance;
    public float OffsetRad;
    public float Speed;
    public float Range;
    public float Density;
    public float Power;
    public float Diameter = 1.0f;
    public int ParticleCount;
    public float Temperature;
    public float LaunchSpeed;
    public float Force;
    public float Duration;
}

// The integer stats both clients simulate with (C++ SpellQuant::Stats)
public sealed record QuantizedStats(
    [property: JsonPropertyName("valid")] bool Valid,
    [property: JsonPropertyName("kind")] byte Kind,
    [property: JsonPropertyName("element")] byte Element,
    [property: JsonPropertyName("imbalance")] int Imbalance,
    [property: JsonPropertyName("offset")] int Offset,
    [property: JsonPropertyName("speed")] int Speed,
    [property: JsonPropertyName("range")] int Range,
    [property: JsonPropertyName("density")] int Density,
    [property: JsonPropertyName("power")] int Power,
    [property: JsonPropertyName("diameter")] int Diameter,
    [property: JsonPropertyName("particleCount")] int ParticleCount,
    [property: JsonPropertyName("temperature")] int Temperature,
    [property: JsonPropertyName("launchSpeed")] int LaunchSpeed,
    [property: JsonPropertyName("force")] int Force,
    [property: JsonPropertyName("duration")] int Duration);
