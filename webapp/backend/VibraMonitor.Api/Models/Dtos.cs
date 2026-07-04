namespace VibraMonitor.Api.Models;

/// <summary>Payload de ingestão enviado pelo ESP32 (ou pelo simulador).</summary>
public record IngestDto
{
    public string? DeviceId { get; init; }

    /// <summary>Opcional: se ausente, o servidor carimba o horário de chegada.</summary>
    public DateTimeOffset? Timestamp { get; init; }

    public double HarmonicDc { get; init; }
    public double Harmonic1st { get; init; }
    public double Harmonic2nd { get; init; }
    public double Harmonic3rd { get; init; }
    public double Harmonic4th { get; init; }

    public double? AccelX { get; init; }
    public double? AccelY { get; init; }
    public double? AccelZ { get; init; }
}

/// <summary>Representação de leitura devolvida pela API (ao dashboard).</summary>
public record ReadingDto(
    long Id,
    string DeviceId,
    DateTimeOffset Timestamp,
    double HarmonicDc,
    double Harmonic1st,
    double Harmonic2nd,
    double Harmonic3rd,
    double Harmonic4th,
    double? AccelX,
    double? AccelY,
    double? AccelZ,
    bool IsAnomaly)
{
    public static ReadingDto From(VibrationReading r) => new(
        r.Id, r.DeviceId, r.Timestamp,
        r.HarmonicDc, r.Harmonic1st, r.Harmonic2nd, r.Harmonic3rd, r.Harmonic4th,
        r.AccelX, r.AccelY, r.AccelZ, r.IsAnomaly);
}

/// <summary>Estatísticas agregadas para os cartões do dashboard.</summary>
public record StatsDto(
    string DeviceId,
    long TotalReadings,
    long AnomalyCount,
    double MaxHarmonic1st,
    double AvgHarmonic1st,
    double AnomalyThreshold,
    DateTimeOffset? LastSeen,
    ReadingDto? Latest);

/// <summary>Resumo de um dispositivo para o seletor do dashboard.</summary>
public record DeviceDto(
    string DeviceId,
    long TotalReadings,
    DateTimeOffset? LastSeen,
    bool HasRecentAnomaly);
