namespace VibraMonitor.Api.Services;

/// <summary>Parâmetros de monitoramento (espelham o firmware).</summary>
public class MonitoringOptions
{
    public const string Section = "Monitoring";

    /// <summary>
    /// Limite crítico da 1ª harmônica. Acima disso a leitura é anomalia.
    /// Corresponde ao LIMITE_CRITICO (100.0) em src/zbus.c do firmware.
    /// </summary>
    public double AnomalyThreshold { get; set; } = 100.0;
}

/// <summary>Configuração do simulador de dados (fase isolada).</summary>
public class SimulatorOptions
{
    public const string Section = "Simulator";

    public bool Enabled { get; set; } = true;
    public int IntervalMs { get; set; } = 1000;
    public string DeviceId { get; set; } = "SIM-01";
}
