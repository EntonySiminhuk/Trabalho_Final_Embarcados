using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Options;
using VibraMonitor.Api.Data;
using VibraMonitor.Api.Hubs;
using VibraMonitor.Api.Models;

namespace VibraMonitor.Api.Services;

/// <summary>
/// Ponto único de entrada de dados: valida/normaliza a leitura, calcula a
/// anomalia, persiste no Postgres e notifica os dashboards via SignalR.
/// Usado tanto pelo endpoint HTTP (ESP32) quanto pelo simulador.
/// </summary>
public class IngestionService(
    AppDbContext db,
    IHubContext<VibrationHub> hub,
    IOptions<MonitoringOptions> monitoring)
{
    private readonly double _threshold = monitoring.Value.AnomalyThreshold;

    public async Task<ReadingDto> IngestAsync(IngestDto dto, CancellationToken ct = default)
    {
        var reading = new VibrationReading
        {
            DeviceId = string.IsNullOrWhiteSpace(dto.DeviceId) ? "unknown" : dto.DeviceId.Trim(),
            Timestamp = (dto.Timestamp ?? DateTimeOffset.UtcNow).ToUniversalTime(),
            HarmonicDc = dto.HarmonicDc,
            Harmonic1st = dto.Harmonic1st,
            Harmonic2nd = dto.Harmonic2nd,
            Harmonic3rd = dto.Harmonic3rd,
            Harmonic4th = dto.Harmonic4th,
            AccelX = dto.AccelX,
            AccelY = dto.AccelY,
            AccelZ = dto.AccelZ,
            IsAnomaly = dto.Harmonic1st > _threshold,
        };

        db.Readings.Add(reading);
        await db.SaveChangesAsync(ct);

        var result = ReadingDto.From(reading);

        // Empurra para os dashboards em tempo real.
        await hub.Clients.All.SendAsync("reading", result, ct);

        return result;
    }
}
