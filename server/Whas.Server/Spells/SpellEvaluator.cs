namespace Whas.Server.Spells;

// Server-side port of SpellSystem::Evaluate (src/spell/spell_system.cpp) and
// SpellQuant::Quantize (src/spell/spell_quant.cpp). Clients never send stats:
// the server computes them from the glyphs and both clients simulate with the
// quantized result. Must stay in step with the C++ side; the golden vectors in
// tests/fixtures/spells.json check that they agree exactly.
public static class SpellEvaluator
{
    // Bump together with SpellQuant::EVALUATOR_VERSION
    public const int Version = 1;

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

    // raylib's DEG2RAD, in float as the C++ side computes it
    const float Deg2Rad = MathF.PI / 180.0f;

    public static SpellKind SigilKind(string assetId) => assetId switch
    {
        "wind" => SpellKind.Flight,
        "gust" => SpellKind.Gust,
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

    public static SpellStats Evaluate(IReadOnlyList<Glyph> glyphs)
    {
        var s = new SpellStats();
        int sigilCount = 0;
        int signCount = 0;
        float sigilScale = 0.0f;

        foreach (var glyph in glyphs)
        {
            if (glyph.IsSigil)
            {
                sigilCount++;
                sigilScale = glyph.Scale;
                s.Kind = SigilKind(glyph.AssetId);
                s.Element = SigilElement(glyph.AssetId);
                continue;
            }
            float rad = glyph.Rotation * Deg2Rad;
            float fx = MathF.Sin(rad);
            float fy = -MathF.Cos(rad);
            s.NetX += fx * glyph.Scale;
            s.NetY += fy * glyph.Scale;
            s.TotalMagnitude += glyph.Scale;
            signCount++;
        }

        s.Valid = sigilCount == 1 && s.Kind != SpellKind.None;

        float lateral = s.NetX;
        float forward = -s.NetY;
        float lateralRatio = 0.0f;
        if (s.TotalMagnitude > 0.0f)
        {
            s.Imbalance = MathF.Min(1.0f,
                Hypot(s.NetX, s.NetY) / s.TotalMagnitude);
            lateralRatio = MathF.Min(1.0f, MathF.Abs(lateral) / s.TotalMagnitude);
            float maxOffset = MaxOffsetDeg * Deg2Rad;
            s.OffsetRad = Math.Clamp(
                MathF.Atan2(lateral, MathF.Max(0.0f, s.TotalMagnitude + forward)),
                -maxOffset, maxOffset);
        }

        s.Speed = BaseSpeed + SpeedPerSign * s.TotalMagnitude *
                  (1.0f - LateralSpeedLoss * lateralRatio);
        s.Range = s.Speed * FlightTime;
        s.Diameter = Math.Clamp(BaseDiameter + sigilScale * DiameterPerSigilScale,
                                1.0f, MaxDiameter);

        switch (s.Kind)
        {
            case SpellKind.Element:
                s.Density = SpellDensity(s.Element) *
                            (DensityBase + DensityPerSigil * sigilScale);
                s.Power = 0.5f * s.Density * s.Speed * s.Speed * PowerScale;
                int count = BaseParticles + (int)(sigilScale * ParticlesPerSigilScale) +
                            signCount * ParticlesPerSign;
                s.ParticleCount = Math.Clamp(count, 1, MaxParticles);
                if (s.Element == Element.Fire)
                    s.Temperature = FireBaseTemp + FireTempPerSigil * sigilScale;
                break;
            case SpellKind.Flight:
                s.LaunchSpeed = MathF.Min(MaxLaunchSpeed,
                    s.Speed * (LaunchBase + LaunchPerSigil * sigilScale));
                break;
            case SpellKind.Gust:
                s.Force = s.Speed * (0.5f + sigilScale) * GustForcePerSpeed;
                s.Duration = GustBaseDuration + GustDurationPerSigil * sigilScale;
                s.Diameter *= GustWidthScale;
                break;
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
        Q(s.Force, StatScale), Q(s.Duration, StatScale));

    public static QuantizedStats EvaluateQuantized(IReadOnlyList<Glyph> glyphs) =>
        Quantize(Evaluate(glyphs));

    // TurnController::CastTicks: 18 ticks + 0.3 per particle, rounded half up
    public static int CastTicks(QuantizedStats stats) =>
        Math.Max(1, (18 * 10 + 3 * stats.ParticleCount + 5) / 10);
}
