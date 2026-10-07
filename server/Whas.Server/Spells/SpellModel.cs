using System.Text.Json.Serialization;

namespace Whas.Server.Spells;

// Mirrors the C++ enums (include/whas/core/element.h, spell_system.h); the
// numbers go over the wire.
public enum Element : byte
{
    Air = 0, Water = 1, Earth = 2, Fire = 3, Steam = 4, Cloud = 5, Ice = 6,
    Sand = 7, Rock = 8, Wood = 9, Grass = 10, Smoke = 11, Light = 12,
}

public enum SpellKind : byte { None = 0, Element = 1, Flight = 2, Field = 3, Compound = 4 }

public enum HomeTarget : byte { None = 0, Human = 1, Element = 2 }

public enum SpellShape : byte { Stream = 0, Orb = 1, Dragon = 2 }

// A glyph as the client's spell files and editor store it
public sealed record Glyph(
    [property: JsonPropertyName("assetId")] string AssetId,
    [property: JsonPropertyName("kind")] string Kind,
    [property: JsonPropertyName("x")] float X,
    [property: JsonPropertyName("y")] float Y,
    [property: JsonPropertyName("scale")] float Scale,
    [property: JsonPropertyName("rotation")] float Rotation,
    // Left out when false, so plain glyphs serialize (and hash) as before
    [property: JsonPropertyName("inverted"),
               JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    bool Inverted = false)
{
    public bool IsSigil => Kind == "sigil";
}

// A single-layer spell embedded in a layered one (C++ SpellComponent)
public sealed record Component(
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("x")] float X,
    [property: JsonPropertyName("y")] float Y,
    [property: JsonPropertyName("scale")] float Scale,
    [property: JsonPropertyName("rotation")] float Rotation,
    [property: JsonPropertyName("glyphs")] List<Glyph> Glyphs);

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
    public SpellShape Shape;
    public float TemperatureDelta;
    public float HardnessScale = 1.0f;
    public float Crush;
    public float Restore;
    public float CollectRadius;
    public int CollectMax;
    public float Pull;
    public float FlashRadius;
    public float FlashTime;
    public HomeTarget HomeTarget;
    public Element HomeElement;
    public float HomeTurnRate;
    public float HomeRadius;
    public float SteerTime;
    public float SteerRate;
    public float HoldTime;
    public float HoldLength;
    public float HoldWidth;
    public float HoldRise;
    public List<SpellStats> Parts = [];
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
    [property: JsonPropertyName("duration")] int Duration,
    [property: JsonPropertyName("shape")] byte Shape = 0,
    [property: JsonPropertyName("temperatureDelta")] int TemperatureDelta = 0,
    [property: JsonPropertyName("hardnessScale")] int HardnessScale = SpellEvaluator.StatScale,
    [property: JsonPropertyName("crush")] int Crush = 0,
    [property: JsonPropertyName("restore")] int Restore = 0,
    [property: JsonPropertyName("collectRadius")] int CollectRadius = 0,
    [property: JsonPropertyName("collectMax")] int CollectMax = 0,
    [property: JsonPropertyName("pull")] int Pull = 0,
    [property: JsonPropertyName("flashRadius")] int FlashRadius = 0,
    [property: JsonPropertyName("flashTime")] int FlashTime = 0,
    [property: JsonPropertyName("homeTarget")] byte HomeTarget = 0,
    [property: JsonPropertyName("homeElement")] byte HomeElement = 0,
    [property: JsonPropertyName("homeTurnRate")] int HomeTurnRate = 0,
    [property: JsonPropertyName("homeRadius")] int HomeRadius = 0,
    [property: JsonPropertyName("steerTime")] int SteerTime = 0,
    [property: JsonPropertyName("steerRate")] int SteerRate = 0,
    [property: JsonPropertyName("holdTime"),
               JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)] int HoldTime = 0,
    [property: JsonPropertyName("holdLength"),
               JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)] int HoldLength = 0,
    [property: JsonPropertyName("holdWidth"),
               JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)] int HoldWidth = 0,
    [property: JsonPropertyName("holdRise"),
               JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)] int HoldRise = 0,
    [property: JsonPropertyName("parts"),
               JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    List<QuantizedStats>? Parts = null)
{
    public bool HasFlight =>
        Kind == (byte)SpellKind.Flight || (Parts?.Any(p => p.HasFlight) ?? false);

    // Parts compare by content (a missing list is the same as an empty one)
    public bool Equals(QuantizedStats? o) =>
        o is not null && Valid == o.Valid && Kind == o.Kind && Element == o.Element &&
        Imbalance == o.Imbalance && Offset == o.Offset && Speed == o.Speed &&
        Range == o.Range && Density == o.Density && Power == o.Power &&
        Diameter == o.Diameter && ParticleCount == o.ParticleCount &&
        Temperature == o.Temperature && LaunchSpeed == o.LaunchSpeed &&
        Force == o.Force && Duration == o.Duration && Shape == o.Shape &&
        TemperatureDelta == o.TemperatureDelta && HardnessScale == o.HardnessScale &&
        Crush == o.Crush && Restore == o.Restore && CollectRadius == o.CollectRadius &&
        CollectMax == o.CollectMax && Pull == o.Pull &&
        FlashRadius == o.FlashRadius && FlashTime == o.FlashTime &&
        HomeTarget == o.HomeTarget && HomeElement == o.HomeElement &&
        HomeTurnRate == o.HomeTurnRate && HomeRadius == o.HomeRadius &&
        SteerTime == o.SteerTime && SteerRate == o.SteerRate &&
        HoldTime == o.HoldTime && HoldLength == o.HoldLength && HoldWidth == o.HoldWidth &&
        HoldRise == o.HoldRise &&
        (Parts ?? []).SequenceEqual(o.Parts ?? []);

    public override int GetHashCode() =>
        HashCode.Combine(Kind, Element, Speed, Power, ParticleCount, Parts?.Count ?? 0);
}
