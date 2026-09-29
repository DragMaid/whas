using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Whas.Server.Data.Migrations
{
    /// <inheritdoc />
    public partial class AddSpellComponents : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<string>(
                name: "ComponentsJson",
                table: "Spells",
                type: "jsonb",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "ComponentsJson",
                table: "Spells");
        }
    }
}
