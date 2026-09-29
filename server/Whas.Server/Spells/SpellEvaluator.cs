namespace Whas.Server.Spells;

// Server-side port of SpellSystem::Evaluate (src/spell/spell_system.cpp) and
// SpellQuant::Quantize (src/spell/spell_quant.cpp). Clients never send stats:
// the server computes them from the glyphs and both clients simulate with the
// quantized result. Must stay in step with the C++ side; the golden vectors in
// tests/fixtures/spells.json check that they agree exactly.
public static class SpellEvaluator
{
    // Bump together with SpellQuant::EVALUATOR_VERSION
    public const int Version = 5;

    public const int StatScale = 1024;
    public const int AngleScale = 65536;
    public const int AimScale = 16384;

    // SpellTuning in spell_system.cpp
    const float BaseSpeed = 40.0f;
    const float SpeedPerSign = 15.0f;
    const float LateralSpeedLoss = 0.5f;
    const float FlightTime = 0.8f;
    const float MaxOffsetDeg = 60.0f;
    const float PowerScale = 0.02f;
    const float DensityBase = 0.8f;
    const float DensityPerSigil = 0.2f;
    const float BaseDiameter = 2.0f;
    const float DiameterPerSigilScale = 4.0f;
    const float MaxDiameter = 12.0f;
    const int BaseParticles = 20;
    const float ParticlesPerSigilScale = 40.0f;
    const int ParticlesPerSign = 4;
    const int MaxParticles = 200;
    const float FireBaseTemp = 1200.0f;
    const float FireTempPerSigil = 800.0f;
    const float LaunchBase = 0.35f;
    const float LaunchPerSigil = 0.35f;
    const float MaxLaunchSpeed = 120.0f;
    const float GustForcePerSpeed = 0.6f;
    const float GustBaseDuration = 0.35f;
    const float GustDurationPerSigil = 0.3f;
    const float GustWidthScale = 1.5f;
    const float ConvergenceDensity = 0.5f;
    const float ConvergenceNarrow = 0.4f;
    const float ConvergenceHardness = 0.25f;
    const float ExpansionDiameter = 0.4f;
    const float MaxExpandedDiameter = 20.0f;
    const float StrengthHardness = 0.6f;
    const float CoolingPerSign = 400.0f;
    const float WaterFreezeDrop = 15.0f;
    const float FireIgniteTemp = 800.0f;
    const float EarthToRockHardness = 1.5f;
    const float CollectBaseRadius = 6.0f;
    const float CollectRadiusPerSign = 10.0f;
    const float CollectCellsPerSign = 40.0f;
    const int MaxCollect = 150;
    const float ComponentFullScale = 0.4f;
    const float ComponentMinEffect = 0.5f;
    const float ComponentMaxEffect = 1.25f;
    public const int MaxComponents = 5;

    // raylib's DEG2RAD, in float as the C++ side computes it
    const float Deg2Rad = MathF.PI / 180.0f;

    public static SpellKind SigilKind(string assetId) => assetId switch
    {
        "wind_underfoot" => SpellKind.Flight,
        "wind" => SpellKind.Gust,
        _ => SigilElement(assetId) != Element.Air ? SpellKind.Element
                                                  : SpellKind.None,
    };

    public static Element SigilElement(string assetId) => assetId switch
    {
        "water" => Element.Water,
        "fire" => Element.Fire,
        "earth" => Element.Earth,
        "ice" => Element.Ice,
        "sand" => Element.Sand,
        "rock" => Element.Rock,
        _ => Element.Air,
    };

    static float SpellDensity(Element e) => e switch
    {
        Element.Water => 1.0f,
        Element.Ice => 0.9f,
        Element.Sand => 1.6f,
        Element.Earth => 2.0f,
        Element.Rock => 2.5f,
        Element.Fire => 0.2f,
        Element.Steam => 0.1f,
        Element.Cloud => 0.05f,
        _ => 1.0f,
    };

    // ShapeTrigger in src/spell/spell_shapes.cpp, in the same order: which
    // glyph picks a shape and how much more material it gathers
    // (count x (ParticlesBase + ParticlesPerScale * summed scale)).
    // Multipliers stack in this order; the last present shape wins.
    readonly record struct ShapeTrigger(SpellShape Shape, string Glyph, bool Sigil,
                                        float ParticlesBase, float ParticlesPerScale);

    static readonly ShapeTrigger[] ShapeTriggers =
    [
        new(SpellShape.Orb, "orb", false, 1.0f, 0.0f),
        new(SpellShape.Dragon, "dragon", true, 1.0f, 0.0f),
    ];

    static ShapeTrigger? TriggerFor(string glyph, bool sigil)
    {
        foreach (var t in ShapeTriggers)
            if (t.Sigil == sigil && t.Glyph == glyph)
                return t;
        return null;
    }

    public static bool IsShapeSigil(string assetId) => TriggerFor(assetId, true) is not null;

    public static bool SignInvertible(string assetId) =>
        assetId is "crushing" or "expansion";

    public static float ComponentEffectiveness(float scale) =>
        Math.Clamp(scale / ComponentFullScale, ComponentMinEffect, ComponentMaxEffect);

    // Summed scales of a circle's modifier signs (crushing and expansion
    // negative when inverted); summed in glyph order like the C++
    struct Modifiers
    {
        public float Convergence, Crush, Repetition, Cooling, Strengthening,
                     Collection, Expansion;
        // Summed scales of each shape's trigger glyphs, by SpellShape
        public float[] Shapes;

        public static Modifiers operator +(Modifiers a, Modifiers b) => new()
        {
            Convergence = a.Convergence + b.Convergence,
            Crush = a.Crush + b.Crush,
            Repetition = a.Repetition + b.Repetition,
            Cooling = a.Cooling + b.Cooling,
            Strengthening = a.Strengthening + b.Strengthening,
            Collection = a.Collection + b.Collection,
            Expansion = a.Expansion + b.Expansion,
            Shapes = a.Shapes.Zip(b.Shapes, (x, y) => x + y).ToArray(),
        };
    }

    sealed class Circle
    {
        public int SigilCount, ShapeSigils, ThrustSigns;
        public float SigilScale;
        public SpellKind Kind;
        public Element Element;
        public float NetX, NetY, Magnitude;
        public Modifiers Mods = new() { Shapes = new float[ShapeCount] };
    }

    static readonly int ShapeCount = Enum.GetValues<SpellShape>().Length;

    static Circle ReadCircle(IReadOnlyList<Glyph> glyphs)
    {
        var c = new Circle();
        foreach (var glyph in glyphs)
        {
            string id = glyph.AssetId;
            if (TriggerFor(id, glyph.IsSigil) is { } trigger)
            {
                c.Mods.Shapes[(int)trigger.Shape] += glyph.Scale;
                if (glyph.IsSigil)
                    c.ShapeSigils++;
                continue;
            }
            if (glyph.IsSigil)
            {
                c.SigilCount++;
                c.SigilScale = glyph.Scale;
                c.Kind = SigilKind(id);
                c.Element = SigilElement(id);
                continue;
            }
            float sign = glyph.Inverted ? -glyph.Scale : glyph.Scale;
            switch (id)
            {
                case "convergence": c.Mods.Convergence += glyph.Scale; break;
                case "crushing": c.Mods.Crush += sign; break;
                case "repetition": c.Mods.Repetition += glyph.Scale; break;
                case "cooling": c.Mods.Cooling += glyph.Scale; break;
                case "strengthening": c.Mods.Strengthening += glyph.Scale; break;
                case "collection": c.Mods.Collection += glyph.Scale; break;
                case "expansion": c.Mods.Expansion += sign; break;
                default:
                    // Column: a thrust vector
                    float rad = glyph.Rotation * Deg2Rad;
                    float fx = MathF.Sin(rad);
                    float fy = -MathF.Cos(rad);
                    c.NetX += fx * glyph.Scale;
                    c.NetY += fy * glyph.Scale;
                    c.Magnitude += glyph.Scale;
                    c.ThrustSigns++;
                    break;
            }
        }
        return c;
    }

    readonly record struct Thrust(float Imbalance, float OffsetRad, float SpeedGain);

    static Thrust ReadThrust(Circle c)
    {
        float lateral = c.NetX;
        float forward = -c.NetY;
        float lateralRatio = 0.0f;
        float imbalance = 0.0f, offset = 0.0f;
        if (c.Magnitude > 0.0f)
        {
            imbalance = MathF.Min(1.0f, Hypot(c.NetX, c.NetY) / c.Magnitude);
            lateralRatio = MathF.Min(1.0f, MathF.Abs(lateral) / c.Magnitude);
            float maxOffset = MaxOffsetDeg * Deg2Rad;
            offset = Math.Clamp(
                MathF.Atan2(lateral, MathF.Max(0.0f, c.Magnitude + forward)),
                -maxOffset, maxOffset);
        }
        float gain = SpeedPerSign * c.Magnitude * (1.0f - LateralSpeedLoss * lateralRatio);
        return new Thrust(imbalance, offset, gain);
    }

    static float Expand(float value, float e)
    {
        if (e == 0.0f)
            return value;
        float f = 1.0f + ExpansionDiameter * MathF.Abs(e);
        return e > 0.0f ? value * f : value / f;
    }

    static SpellStats Build(Circle c, Modifiers mods, float effect, float speedBonus,
                            float offsetBonus)
    {
        var s = new SpellStats();
        var thrust = ReadThrust(c);
        s.Valid = c.SigilCount == 1 && c.Kind != SpellKind.None && c.ShapeSigils <= 1;
        s.Kind = c.Kind;
        s.Element = c.Element;
        s.NetX = c.NetX;
        s.NetY = c.NetY;
        s.TotalMagnitude = c.Magnitude;
        s.Imbalance = thrust.Imbalance;
        s.OffsetRad = thrust.OffsetRad + offsetBonus;

        float sigilScale = c.SigilScale;
        s.Speed = BaseSpeed + thrust.SpeedGain + speedBonus;
        s.Range = s.Speed * FlightTime;
        s.Diameter = Math.Clamp(BaseDiameter + sigilScale * DiameterPerSigilScale,
                                1.0f, MaxDiameter);

        switch (s.Kind)
        {
            case SpellKind.Element:
            {
                s.Density = SpellDensity(s.Element) * (DensityBase + DensityPerSigil * sigilScale);
                int count = BaseParticles + (int)(sigilScale * ParticlesPerSigilScale) +
                            c.ThrustSigns * ParticlesPerSign;
                if (s.Element == Element.Fire)
                    s.Temperature = FireBaseTemp + FireTempPerSigil * sigilScale;

                if (mods.Convergence > 0.0f)
                {
                    s.Density *= 1.0f + ConvergenceDensity * mods.Convergence;
                    s.Diameter /= 1.0f + ConvergenceNarrow * mods.Convergence;
                    s.HardnessScale *= 1.0f + ConvergenceHardness * mods.Convergence;
                }
                if (mods.Expansion != 0.0f)
                {
                    s.Diameter = Expand(s.Diameter, mods.Expansion);
                    float f = 1.0f + ExpansionDiameter * MathF.Abs(mods.Expansion);
                    float pf = f * f;
                    count = (int)(mods.Expansion > 0.0f ? count * pf : count / pf);
                }
                s.Diameter = Math.Clamp(s.Diameter, 1.0f, MaxExpandedDiameter);
                // Shape glyphs: multipliers stack, the last listed shape wins
                foreach (var t in ShapeTriggers)
                {
                    float sum = mods.Shapes[(int)t.Shape];
                    if (sum <= 0.0f)
                        continue;
                    s.Shape = t.Shape;
                    count = (int)(count * (t.ParticlesBase + t.ParticlesPerScale * sum));
                }
                if (mods.Strengthening > 0.0f)
                    s.HardnessScale *= 1.0f + StrengthHardness * mods.Strengthening;
                if (mods.Repetition > 0.0f)
                {
                    s.Restore = mods.Repetition;
                    s.Temperature = 0.0f;
                    s.HardnessScale = 1.0f;
                }
                else if (mods.Cooling > 0.0f)
                {
                    // Cooled as cast: water freezes, fire too cold to ignite
                    // is smoke, anything else lands cold
                    float drop = CoolingPerSign * mods.Cooling;
                    if (s.Element == Element.Fire)
                    {
                        s.Temperature -= drop;
                        if (s.Temperature < FireIgniteTemp)
                        {
                            s.Element = Element.Smoke;
                            s.Temperature = 0.0f;
                        }
                    }
                    else
                    {
                        s.TemperatureDelta = -drop;
                        if (s.Element == Element.Water && drop >= WaterFreezeDrop)
                            s.Element = Element.Ice;
                    }
                }
                // Earth hardened enough is rock
                if (s.Element == Element.Earth && s.HardnessScale >= EarthToRockHardness)
                {
                    s.Element = Element.Rock;
                    s.HardnessScale = 1.0f;
                }
                s.Crush = mods.Crush;
                if (mods.Collection > 0.0f)
                {
                    s.CollectRadius = CollectBaseRadius + CollectRadiusPerSign * mods.Collection;
                    s.CollectMax = Math.Min(MaxCollect, (int)(CollectCellsPerSign * mods.Collection));
                }

                s.Power = 0.5f * s.Density * s.Speed * s.Speed * PowerScale;
                s.ParticleCount = Math.Clamp((int)(count * effect), 1, MaxParticles);
                break;
            }
            case SpellKind.Flight:
                s.LaunchSpeed = MathF.Min(MaxLaunchSpeed,
                    s.Speed * (LaunchBase + LaunchPerSigil * sigilScale) * effect);
                break;
            case SpellKind.Gust:
                s.Force = s.Speed * (0.5f + sigilScale) * GustForcePerSpeed * effect;
                s.Duration = GustBaseDuration + GustDurationPerSigil * sigilScale;
                s.Diameter = Expand(s.Diameter * GustWidthScale, mods.Expansion);
                break;
        }
        return s;
    }

    public static SpellStats Evaluate(IReadOnlyList<Glyph> glyphs) =>
        Evaluate(glyphs, []);

    public static SpellStats Evaluate(IReadOnlyList<Glyph> glyphs,
                                      IReadOnlyList<Component> components)
    {
        if (components.Count == 0)
        {
            var c = ReadCircle(glyphs);
            return Build(c, c.Mods, 1.0f, 0.0f, 0.0f);
        }

        var outer = ReadCircle(glyphs);
        var thrust = ReadThrust(outer);
        var s = new SpellStats
        {
            Kind = SpellKind.Compound,
            NetX = outer.NetX,
            NetY = outer.NetY,
            TotalMagnitude = outer.Magnitude,
            Imbalance = thrust.Imbalance,
            OffsetRad = thrust.OffsetRad,
            Speed = BaseSpeed + thrust.SpeedGain,
        };
        s.Valid = outer.SigilCount == 0 && outer.ShapeSigils == 0 &&
                  components.Count >= 1 && components.Count <= MaxComponents;
        foreach (var component in components)
        {
            var inner = ReadCircle(component.Glyphs);
            var part = Build(inner, inner.Mods + outer.Mods,
                             ComponentEffectiveness(component.Scale), thrust.SpeedGain,
                             thrust.OffsetRad + component.Rotation * Deg2Rad);
            s.Valid = s.Valid && part.Valid;
            s.Parts.Add(part);
        }
        return s;
    }

    // std::hypot on floats: computed in double and rounded once
    static float Hypot(float x, float y) => (float)Math.Sqrt((double)x * x + (double)y * y);

    static int Q(float v, int scale) =>
        (int)Math.Round((double)v * scale, MidpointRounding.AwayFromZero);

    public static QuantizedStats Quantize(SpellStats s) => new(
        s.Valid, (byte)s.Kind, (byte)s.Element,
        Q(s.Imbalance, StatScale), Q(s.OffsetRad, AngleScale),
        Q(s.Speed, StatScale), Q(s.Range, StatScale), Q(s.Density, StatScale),
        Q(s.Power, StatScale), Q(s.Diameter, StatScale), s.ParticleCount,
        Q(s.Temperature, StatScale), Q(s.LaunchSpeed, StatScale),
        Q(s.Force, StatScale), Q(s.Duration, StatScale),
        (byte)s.Shape, Q(s.TemperatureDelta, StatScale), Q(s.HardnessScale, StatScale),
        Q(s.Crush, StatScale), Q(s.Restore, StatScale), Q(s.CollectRadius, StatScale),
        s.CollectMax,
        s.Parts.Count == 0 ? null : s.Parts.Select(Quantize).ToList());

    public static QuantizedStats EvaluateQuantized(IReadOnlyList<Glyph> glyphs) =>
        Quantize(Evaluate(glyphs));

    public static QuantizedStats EvaluateQuantized(IReadOnlyList<Glyph> glyphs,
                                                   IReadOnlyList<Component> components) =>
        Quantize(Evaluate(glyphs, components));

    // TurnController::CastTicks: 18 ticks + 0.3 per particle, rounded half up.
    // A layered spell: its slowest part, half the others (rounded up), 8 to
    // bind the layers and 2 per extra part.
    public static int CastTicks(QuantizedStats stats)
    {
        if (stats.Kind == (byte)SpellKind.Compound)
        {
            var parts = stats.Parts ?? [];
            int longest = 0, sum = 0;
            foreach (var part in parts)
            {
                int t = CastTicks(part);
                longest = Math.Max(longest, t);
                sum += t;
            }
            int rest = (sum - longest + 1) / 2;
            return Math.Max(1, longest + rest + 8 + 2 * Math.Max(0, parts.Count - 1));
        }
        return Math.Max(1, (18 * 10 + 3 * stats.ParticleCount + 5) / 10);
    }
}
