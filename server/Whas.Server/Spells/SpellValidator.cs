namespace Whas.Server.Spells;

// Rejects glyph data a real editor couldn't have produced. (Overlap between
// glyphs needs the SVG shapes and is only checked by the client editor.)
public static class SpellValidator
{
    public const int MaxGlyphs = 32;
    public const int MaxNameLength = 32;
    const float OuterRadius = 250.0f;
    const float MinScale = 0.1f, MaxScale = 3.0f;

    static readonly HashSet<string> KnownSigils =
        ["water", "fire", "earth", "ice", "sand", "rock", "wind", "gust"];

    public static string? Check(string name, IReadOnlyList<Glyph> glyphs)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Length > MaxNameLength)
            return "name must be 1-32 characters";
        if (glyphs.Count == 0 || glyphs.Count > MaxGlyphs)
            return $"a spell has 1-{MaxGlyphs} glyphs";
        int sigils = 0, signs = 0;
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
                sigils++;
                if (!KnownSigils.Contains(g.AssetId))
                    return "unknown sigil";
            }
            else signs++;
        }
        if (sigils != 1)
            return "a spell needs exactly one sigil";
        if (signs == 0)
            return "a spell needs at least one sign";
        return null;
    }
}
