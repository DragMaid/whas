using System.Text.Json;
using Whas.Server.Net;

namespace Whas.Server.Matches;

// What a room plays by, chosen by its host: up to three maps the rounds take
// turns on (generated or player-made) and the chaos / real-time modes. The
// server doesn't build maps; it checks the shape and size and hands the same
// JSON to both clients (and to replays).
public sealed record RoomOptions(JsonElement Json, bool Chaos, bool Rts)
{
    public const int MaxMaps = 3;
    public const int MaxMapBytes = 64 * 1024;

    public static readonly RoomOptions Default = new(
        JsonSerializer.SerializeToElement(new { maps = Array.Empty<object>(), chaos = false, rts = false }),
        false, false);

    // Missing options mean the defaults
    public static bool TryParse(JsonElement msg, out RoomOptions options, out string error)
    {
        options = Default;
        error = "";
        if (!msg.TryGetProperty("options", out var o) || o.ValueKind == JsonValueKind.Null)
            return true;
        if (o.ValueKind != JsonValueKind.Object)
            return Fail("room options must be an object", out error);

        var maps = new List<JsonElement>();
        if (o.TryGetProperty("maps", out var m))
        {
            if (m.ValueKind != JsonValueKind.Array || m.GetArrayLength() > MaxMaps)
                return Fail($"a room takes at most {MaxMaps} maps", out error);
            foreach (var map in m.EnumerateArray())
            {
                string kind = map.ValueKind == JsonValueKind.Object ? map.OptStr("kind") ?? "" : "";
                if (kind == "random")
                {
                    maps.Add(JsonSerializer.SerializeToElement(new { kind }));
                    continue;
                }
                if (kind != "custom" || !map.TryGetProperty("map", out var def) ||
                    def.ValueKind != JsonValueKind.Object)
                    return Fail("unknown map entry", out error);
                if (def.GetRawText().Length > MaxMapBytes)
                    return Fail("that map is too large", out error);
                maps.Add(JsonSerializer.SerializeToElement(new { kind, map = def }));
            }
        }
        bool chaos = Flag(o, "chaos"), rts = Flag(o, "rts");
        options = new RoomOptions(
            JsonSerializer.SerializeToElement(new { maps, chaos, rts }), chaos, rts);
        return true;
    }

    static bool Flag(JsonElement o, string name) =>
        o.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;

    static bool Fail(string reason, out string error)
    {
        error = reason;
        return false;
    }
}
