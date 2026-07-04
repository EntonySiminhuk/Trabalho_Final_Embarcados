namespace VibraMonitor.Api.Models;

/// <summary>
/// Uma leitura de vibração vinda do sensor (STM32 + MPU6050), com as
/// harmônicas da FFT já calculadas no embarcado (ver src/zbus.c do firmware).
/// </summary>
public class VibrationReading
{
    public long Id { get; set; }

    /// <summary>Identificador do dispositivo/placa que gerou a leitura.</summary>
    public string DeviceId { get; set; } = "unknown";

    /// <summary>Instante da leitura (UTC).</summary>
    public DateTimeOffset Timestamp { get; set; }

    // --- Espectro de vibração (magnitudes da FFT, eixo X) ---
    public double HarmonicDc { get; set; }
    public double Harmonic1st { get; set; }
    public double Harmonic2nd { get; set; }
    public double Harmonic3rd { get; set; }
    public double Harmonic4th { get; set; }

    // --- Aceleração instantânea bruta (opcional; do comando show_raw) ---
    public double? AccelX { get; set; }
    public double? AccelY { get; set; }
    public double? AccelZ { get; set; }

    /// <summary>
    /// Marca de anomalia: 1ª harmônica acima do limite crítico.
    /// Calculada no momento da ingestão, espelhando o LIMITE_CRITICO do firmware.
    /// </summary>
    public bool IsAnomaly { get; set; }
}
