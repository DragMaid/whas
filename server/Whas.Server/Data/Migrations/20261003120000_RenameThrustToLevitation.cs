using Microsoft.EntityFrameworkCore.Infrastructure;
using Microsoft.EntityFrameworkCore.Migrations;
using Whas.Server.Data;

#nullable disable

namespace Whas.Server.Data.Migrations
{
    // Data only: the thrust sign "column" is now "levitation". Rows keep
    // their evaluator version, so SpellService re-evaluates their stats the
    // next time they're read (the rename there is a no-op by then).
    [DbContext(typeof(WhasDb))]
    [Migration("20261003120000_RenameThrustToLevitation")]
    public partial class RenameThrustToLevitation : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql("""
                UPDATE "Spells"
                SET "GlyphsJson" = replace("GlyphsJson"::text, '"assetId": "column"', '"assetId": "levitation"')::jsonb,
                    "ComponentsJson" = replace("ComponentsJson"::text, '"assetId": "column"', '"assetId": "levitation"')::jsonb
                WHERE "EvaluatorVersion" < 7;
                """);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql("""
                UPDATE "Spells"
                SET "GlyphsJson" = replace("GlyphsJson"::text, '"assetId": "levitation"', '"assetId": "column"')::jsonb,
                    "ComponentsJson" = replace("ComponentsJson"::text, '"assetId": "levitation"', '"assetId": "column"')::jsonb
                WHERE "EvaluatorVersion" < 7;
                """);
        }
    }
}
