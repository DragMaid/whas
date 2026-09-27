using System.Security.Cryptography;
using System.Text;
using Microsoft.EntityFrameworkCore;
using Whas.Server.Data;

namespace Whas.Server.Players;

// Guest accounts: the server hands out a random token, the client keeps it
// (data/guest.json) and presents it on every connection.
public sealed class PlayerService(IDbContextFactory<WhasDb> dbFactory)
{
    public static string HashToken(string token) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(token)));

    public async Task<Player?> FindAsync(string? token, CancellationToken ct = default)
    {
        if (string.IsNullOrWhiteSpace(token) || token.Length > 128)
            return null;
        await using var db = await dbFactory.CreateDbContextAsync(ct);
        string hash = HashToken(token);
        return await db.Players.AsNoTracking().FirstOrDefaultAsync(p => p.TokenHash == hash, ct);
    }

    public async Task<(Player Player, string Token)> CreateGuestAsync(CancellationToken ct = default)
    {
        string token = Convert.ToBase64String(RandomNumberGenerator.GetBytes(32))
            .TrimEnd('=').Replace('+', '-').Replace('/', '_');
        var player = new Player { TokenHash = HashToken(token), CreatedAt = DateTimeOffset.UtcNow };
        await using var db = await dbFactory.CreateDbContextAsync(ct);
        db.Players.Add(player);
        await db.SaveChangesAsync(ct);
        return (player, token);
    }

    // For REST: "Authorization: Bearer <token>"
    public Task<Player?> FromRequestAsync(HttpRequest request, CancellationToken ct = default)
    {
        string? header = request.Headers.Authorization;
        const string prefix = "Bearer ";
        return FindAsync(header is not null && header.StartsWith(prefix, StringComparison.Ordinal)
                             ? header[prefix.Length..] : null, ct);
    }
}
