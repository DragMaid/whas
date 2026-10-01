using Microsoft.EntityFrameworkCore;

namespace Whas.Server.Data;

public sealed class WhasDb(DbContextOptions<WhasDb> options) : DbContext(options)
{
    public DbSet<Player> Players => Set<Player>();
    public DbSet<SpellDefinition> Spells => Set<SpellDefinition>();
    public DbSet<Deck> Decks => Set<Deck>();
    public DbSet<Match> Matches => Set<Match>();
    public DbSet<MatchPlayer> MatchPlayers => Set<MatchPlayer>();
    public DbSet<TurnRecord> Turns => Set<TurnRecord>();
    public DbSet<SpellUsage> SpellUsage => Set<SpellUsage>();
    public DbSet<DesyncReport> DesyncReports => Set<DesyncReport>();

    protected override void OnModelCreating(ModelBuilder b)
    {
        b.Entity<Player>().HasIndex(p => p.TokenHash).IsUnique();

        b.Entity<SpellDefinition>(e =>
        {
            e.Property(s => s.GlyphsJson).HasColumnType("jsonb");
            e.Property(s => s.ComponentsJson).HasColumnType("jsonb");
            e.Property(s => s.StatsJson).HasColumnType("jsonb");
            e.HasIndex(s => new { s.OwnerId, s.GlyphsHash, s.EvaluatorVersion });
            e.HasOne<Player>().WithMany().HasForeignKey(s => s.OwnerId);
        });

        b.Entity<Deck>(e =>
        {
            e.HasIndex(d => d.OwnerId);
            e.HasOne<Player>().WithMany().HasForeignKey(d => d.OwnerId);
        });

        b.Entity<Match>(e =>
        {
            e.Property(m => m.Mode).HasConversion<string>();
            e.Property(m => m.Status).HasConversion<string>();
            e.Property(m => m.OptionsJson).HasColumnType("jsonb").HasDefaultValueSql("'{}'::jsonb");
            e.HasMany(m => m.Players).WithOne().HasForeignKey(p => p.MatchId);
        });

        b.Entity<MatchPlayer>(e =>
        {
            e.HasKey(p => new { p.MatchId, p.Slot });
            e.HasIndex(p => p.PlayerId);
            e.Property(p => p.RoundDecksJson).HasColumnType("jsonb");
        });

        b.Entity<TurnRecord>(e =>
        {
            e.HasKey(t => new { t.MatchId, t.Round, t.Turn });
            e.Property(t => t.PlanSlot0Json).HasColumnType("jsonb");
            e.Property(t => t.PlanSlot1Json).HasColumnType("jsonb");
        });

        b.Entity<SpellUsage>().HasKey(u => new { u.SpellId, u.MatchId });
        b.Entity<DesyncReport>().HasIndex(d => d.SuspectPlayerId);
    }
}
