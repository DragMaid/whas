using System;
using Microsoft.EntityFrameworkCore.Migrations;
using Npgsql.EntityFrameworkCore.PostgreSQL.Metadata;

#nullable disable

namespace Whas.Server.Data.Migrations
{
    /// <inheritdoc />
    public partial class Initial : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.CreateTable(
                name: "DesyncReports",
                columns: table => new
                {
                    Id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    MatchId = table.Column<long>(type: "bigint", nullable: false),
                    Round = table.Column<int>(type: "integer", nullable: false),
                    Turn = table.Column<int>(type: "integer", nullable: false),
                    HashA = table.Column<string>(type: "text", nullable: false),
                    HashB = table.Column<string>(type: "text", nullable: false),
                    ReferenceSlot = table.Column<int>(type: "integer", nullable: false),
                    BuildIdA = table.Column<string>(type: "text", nullable: true),
                    BuildIdB = table.Column<string>(type: "text", nullable: true),
                    SuspectPlayerId = table.Column<long>(type: "bigint", nullable: false),
                    At = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_DesyncReports", x => x.Id);
                });

            migrationBuilder.CreateTable(
                name: "Matches",
                columns: table => new
                {
                    Id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    Seed = table.Column<long>(type: "bigint", nullable: false),
                    Mode = table.Column<string>(type: "text", nullable: false),
                    RulesetVersion = table.Column<int>(type: "integer", nullable: false),
                    BuildId = table.Column<string>(type: "text", nullable: false),
                    StartedAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    EndedAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true),
                    WinnerId = table.Column<long>(type: "bigint", nullable: true),
                    Status = table.Column<string>(type: "text", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_Matches", x => x.Id);
                });

            migrationBuilder.CreateTable(
                name: "Players",
                columns: table => new
                {
                    Id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    TokenHash = table.Column<string>(type: "text", nullable: false),
                    CreatedAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    Wins = table.Column<int>(type: "integer", nullable: false),
                    Losses = table.Column<int>(type: "integer", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_Players", x => x.Id);
                });

            migrationBuilder.CreateTable(
                name: "SpellUsage",
                columns: table => new
                {
                    SpellId = table.Column<long>(type: "bigint", nullable: false),
                    MatchId = table.Column<long>(type: "bigint", nullable: false),
                    Casts = table.Column<int>(type: "integer", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_SpellUsage", x => new { x.SpellId, x.MatchId });
                });

            migrationBuilder.CreateTable(
                name: "Turns",
                columns: table => new
                {
                    MatchId = table.Column<long>(type: "bigint", nullable: false),
                    Round = table.Column<int>(type: "integer", nullable: false),
                    Turn = table.Column<int>(type: "integer", nullable: false),
                    PlanSlot0Json = table.Column<string>(type: "jsonb", nullable: false),
                    PlanSlot1Json = table.Column<string>(type: "jsonb", nullable: false),
                    HashSlot0 = table.Column<string>(type: "text", nullable: true),
                    HashSlot1 = table.Column<string>(type: "text", nullable: true),
                    At = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_Turns", x => new { x.MatchId, x.Round, x.Turn });
                });

            migrationBuilder.CreateTable(
                name: "MatchPlayers",
                columns: table => new
                {
                    MatchId = table.Column<long>(type: "bigint", nullable: false),
                    Slot = table.Column<int>(type: "integer", nullable: false),
                    PlayerId = table.Column<long>(type: "bigint", nullable: false),
                    RoundDecksJson = table.Column<string>(type: "jsonb", nullable: false),
                    RoundsWon = table.Column<int>(type: "integer", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_MatchPlayers", x => new { x.MatchId, x.Slot });
                    table.ForeignKey(
                        name: "FK_MatchPlayers_Matches_MatchId",
                        column: x => x.MatchId,
                        principalTable: "Matches",
                        principalColumn: "Id",
                        onDelete: ReferentialAction.Cascade);
                });

            migrationBuilder.CreateTable(
                name: "Decks",
                columns: table => new
                {
                    Id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    OwnerId = table.Column<long>(type: "bigint", nullable: false),
                    Name = table.Column<string>(type: "text", nullable: false),
                    SpellIds = table.Column<long[]>(type: "bigint[]", nullable: false),
                    UpdatedAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_Decks", x => x.Id);
                    table.ForeignKey(
                        name: "FK_Decks_Players_OwnerId",
                        column: x => x.OwnerId,
                        principalTable: "Players",
                        principalColumn: "Id",
                        onDelete: ReferentialAction.Cascade);
                });

            migrationBuilder.CreateTable(
                name: "Spells",
                columns: table => new
                {
                    Id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    OwnerId = table.Column<long>(type: "bigint", nullable: false),
                    Name = table.Column<string>(type: "text", nullable: false),
                    GlyphsJson = table.Column<string>(type: "jsonb", nullable: false),
                    StatsJson = table.Column<string>(type: "jsonb", nullable: false),
                    GlyphsHash = table.Column<string>(type: "text", nullable: false),
                    EvaluatorVersion = table.Column<int>(type: "integer", nullable: false),
                    CreatedAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_Spells", x => x.Id);
                    table.ForeignKey(
                        name: "FK_Spells_Players_OwnerId",
                        column: x => x.OwnerId,
                        principalTable: "Players",
                        principalColumn: "Id",
                        onDelete: ReferentialAction.Cascade);
                });

            migrationBuilder.CreateIndex(
                name: "IX_Decks_OwnerId",
                table: "Decks",
                column: "OwnerId");

            migrationBuilder.CreateIndex(
                name: "IX_DesyncReports_SuspectPlayerId",
                table: "DesyncReports",
                column: "SuspectPlayerId");

            migrationBuilder.CreateIndex(
                name: "IX_MatchPlayers_PlayerId",
                table: "MatchPlayers",
                column: "PlayerId");

            migrationBuilder.CreateIndex(
                name: "IX_Players_TokenHash",
                table: "Players",
                column: "TokenHash",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "IX_Spells_OwnerId_GlyphsHash_EvaluatorVersion",
                table: "Spells",
                columns: new[] { "OwnerId", "GlyphsHash", "EvaluatorVersion" });
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "Decks");

            migrationBuilder.DropTable(
                name: "DesyncReports");

            migrationBuilder.DropTable(
                name: "MatchPlayers");

            migrationBuilder.DropTable(
                name: "Spells");

            migrationBuilder.DropTable(
                name: "SpellUsage");

            migrationBuilder.DropTable(
                name: "Turns");

            migrationBuilder.DropTable(
                name: "Matches");

            migrationBuilder.DropTable(
                name: "Players");
        }
    }
}
