using Microsoft.EntityFrameworkCore;
using VibraMonitor.Api.Models;

namespace VibraMonitor.Api.Data;

public class AppDbContext(DbContextOptions<AppDbContext> options) : DbContext(options)
{
    public DbSet<VibrationReading> Readings => Set<VibrationReading>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        var reading = modelBuilder.Entity<VibrationReading>();

        reading.HasKey(r => r.Id);
        reading.Property(r => r.DeviceId).HasMaxLength(64).IsRequired();

        // Índices para as consultas mais comuns do dashboard:
        // "últimas leituras deste device" e "leituras num intervalo".
        reading.HasIndex(r => new { r.DeviceId, r.Timestamp });
        reading.HasIndex(r => r.Timestamp);
    }
}
