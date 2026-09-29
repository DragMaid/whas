namespace Whas.Server.Spells;

// Rejects glyph data a real editor couldn't have produced. (Overlap between
// glyphs needs the SVG shapes and is only checked by the client editor.)
public static class SpellValidator
{
    public const int MaxGlyphs = 32;
    public const int MaxNameLength = 32;
    const float OuterRadius = 250.0f;
    const float MinScale = 0.1f, MaxScale = 3.0f;
    // Layered spells (spell_types.h)
    const float LayerCoreRadius = 175.0f;
    const float ComponentScaleMin = 0.2f, ComponentScaleMax = 0.7f;

    static readonly HashSet<string> KnownSigils =
        ["water", "fire", "earth", "ice", "sand", "rock", "wind", "wind_underfoot", "light", "dragon", "guidance", "human"];

    static readonly HashSet<string> KnownSigns =
        ["column", "convergence", "crushing", "repetition", "cooling", "strengthening",
         "collection", "expansion", "orb", "pulling"];

    public static string? Check(string name, IReadOnlyList<Glyph> glyphs) =>
        Check(name, glyphs, []);

    public static string? Check(string name, IReadOnlyList<Glyph> glyphs,
                                IReadOnlyList<Component> components)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Length > MaxNameLength)
            return "name must be 1-32 characters";
        if (components.Count == 0)
            return CheckCircle(glyphs);

        // Layered: the outer ring holds signs only, the core 1-5 plain spells
        if (components.Count > SpellEvaluator.MaxComponents)
            return $"a layered spell holds 1-{SpellEvaluator.MaxComponents} spells";
        if (glyphs.Count > MaxGlyphs)
            return $"a ring has at most {MaxGlyphs} glyphs";
        if (CheckGlyphs(glyphs, out int sigils, out _, out _) is { } bad)
            return bad;
        if (sigils > 0)
            return "the outer ring holds signs only";
        foreach (var c in components)
        {
            if (c.Source is null || c.Source.Length > MaxNameLength)
                return "bad layer name";
            if (!float.IsFinite(c.X) || !float.IsFinite(c.Y) ||
                !float.IsFinite(c.Scale) || !float.IsFinite(c.Rotation))
                return "non-finite layer values";
            if (c.Scale < ComponentScaleMin - 1e-4f || c.Scale > ComponentScaleMax + 1e-4f)
                return "layer scale out of range";
            if (c.Rotation < -180.001f || c.Rotation > 180.001f)
                return "layer rotation out of range";
            if (MathF.Sqrt(c.X * c.X + c.Y * c.Y) + OuterRadius * c.Scale > LayerCoreRadius + 1e-3f)
                return "layer outside the core";
            if (c.Glyphs is null)
                return "layer: missing glyphs";
            if (CheckCircle(c.Glyphs) is { } inner)
                return $"layer: {inner}";
        }
        return null;
    }

    // A plain circle: one sigil (plus at most one dragon, and guidance with
    // its target) and some signs, that the evaluator can make a spell of
    static string? CheckCircle(IReadOnlyList<Glyph> glyphs)
    {
        if (glyphs.Count == 0 || glyphs.Count > MaxGlyphs)
            return $"a spell has 1-{MaxGlyphs} glyphs";
        if (CheckGlyphs(glyphs, out int sigils, out int shapes, out int signs) is { } bad)
            return bad;
        if (sigils < 1 || sigils > 4)
            return "a spell needs one sigil to fire (plus guidance and its target)";
        if (shapes > 1)
            return "a spell holds at most one dragon";
        if (signs == 0)
            return "a spell needs at least one sign";
        if (!SpellEvaluator.Evaluate(glyphs).Valid)
            return "the sigils don't make a spell (one sigil to fire; guidance needs " +
                   "a target, a human sigil needs guidance, wind needs a pulling sign)";
        return null;
    }

    static string? CheckGlyphs(IReadOnlyList<Glyph> glyphs, out int sigils, out int shapes,
                               out int signs)
    {
        sigils = shapes = signs = 0;
        foreach (var g in glyphs)
        {
            if (g.Kind is not ("sign" or "sigil"))
                return "unknown glyph kind";
            if (string.IsNullOrEmpty(g.AssetId) || g.AssetId.Length > 64)
                return "bad asset id";
            if (!float.IsFinite(g.X) || !float.IsFinite(g.Y) ||
                !float.IsFinite(g.Scale) || !float.IsFinite(g.Rotation))
                return "non-finite glyph values";
            if (g.Scale < MinScale - 1e-4f || g.Scale > MaxScale + 1e-4f)
                return "glyph scale out of range";
            if (g.Rotation < -180.001f || g.Rotation > 180.001f)
                return "glyph rotation out of range";
            if (g.X * g.X + g.Y * g.Y > OuterRadius * OuterRadius)
                return "glyph outside the circle";
            if (g.IsSigil)
            {
                if (!KnownSigils.Contains(g.AssetId))
                    return "unknown sigil";
                if (g.Inverted)
                    return "sigils can't be inverted";
                if (SpellEvaluator.IsShapeSigil(g.AssetId))
                    shapes++;
                else
                    sigils++;
            }
            else
            {
                if (!KnownSigns.Contains(g.AssetId))
                    return "unknown sign";
                if (g.Inverted && !SpellEvaluator.SignInvertible(g.AssetId))
                    return "that sign can't be inverted";
                signs++;
            }
        }
        return null;
    }
}
