using System;
using Microsoft.EntityFrameworkCore.Migrations;
using Npgsql.EntityFrameworkCore.PostgreSQL.Metadata;

#nullable disable

namespace VibraMonitor.Api.Migrations
{
    /// <inheritdoc />
    public partial class InitialCreate : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.CreateTable(
                name: "Readings",
                columns: table => new
                {
                    Id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    DeviceId = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    Timestamp = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    HarmonicDc = table.Column<double>(type: "double precision", nullable: false),
                    Harmonic1st = table.Column<double>(type: "double precision", nullable: false),
                    Harmonic2nd = table.Column<double>(type: "double precision", nullable: false),
                    Harmonic3rd = table.Column<double>(type: "double precision", nullable: false),
                    Harmonic4th = table.Column<double>(type: "double precision", nullable: false),
                    AccelX = table.Column<double>(type: "double precision", nullable: true),
                    AccelY = table.Column<double>(type: "double precision", nullable: true),
                    AccelZ = table.Column<double>(type: "double precision", nullable: true),
                    IsAnomaly = table.Column<bool>(type: "boolean", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_Readings", x => x.Id);
                });

            migrationBuilder.CreateIndex(
                name: "IX_Readings_DeviceId_Timestamp",
                table: "Readings",
                columns: new[] { "DeviceId", "Timestamp" });

            migrationBuilder.CreateIndex(
                name: "IX_Readings_Timestamp",
                table: "Readings",
                column: "Timestamp");
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "Readings");
        }
    }
}
