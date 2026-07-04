using Microsoft.Extensions.Options;
using VibraMonitor.Api.Models;

namespace VibraMonitor.Api.Services;

/// <summary>
/// Gera leituras de vibração sintéticas enquanto o hardware não está conectado.
/// Simula uma máquina cuja 1ª harmônica oscila em torno de um valor normal e,
/// de tempos em tempos, sobe acima do limite (anomalia) para exercitar o
/// dashboard. Ligável/desligável por configuração (Simulator:Enabled).
/// </summary>
public class SimulatorService(
    IServiceScopeFactory scopeFactory,
    IOptions<SimulatorOptions> options,
    ILogger<SimulatorService> logger) : BackgroundService
{
    private readonly SimulatorOptions _opts = options.Value;
    private readonly Random _rng = new();

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        if (!_opts.Enabled)
        {
            logger.LogInformation("Simulador DESLIGADO (Simulator:Enabled=false).");
            return;
        }

        logger.LogInformation(
            "Simulador LIGADO: device '{Device}', intervalo {Interval} ms.",
            _opts.DeviceId, _opts.IntervalMs);

        // Aguarda um pouco para o banco/migrations ficarem prontos.
        await Task.Delay(TimeSpan.FromSeconds(2), stoppingToken);

        double t = 0;
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                using var scope = scopeFactory.CreateScope();
                var ingestion = scope.ServiceProvider.GetRequiredService<IngestionService>();
                await ingestion.IngestAsync(BuildSample(t), stoppingToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                logger.LogError(ex, "Falha ao gerar leitura simulada.");
            }

            t += _opts.IntervalMs / 1000.0;
            await Task.Delay(_opts.IntervalMs, stoppingToken);
        }
    }

    private IngestDto BuildSample(double t)
    {
        // 1ª harmônica: base ~40 com ondulação lenta + ruído.
        double baseLevel = 40 + 15 * Math.Sin(t / 8.0);
        double noise = _rng.NextDouble() * 10 - 5;
        double h1 = baseLevel + noise;

        // ~8% das amostras entram em regime de anomalia (pico acima de 100).
        if (_rng.NextDouble() < 0.08)
        {
            h1 += 70 + _rng.NextDouble() * 60;
        }

        h1 = Math.Max(0, h1);

        return new IngestDto
        {
            DeviceId = _opts.DeviceId,
            HarmonicDc = 500 + _rng.NextDouble() * 20,     // componente DC (gravidade residual)
            Harmonic1st = h1,
            Harmonic2nd = h1 * 0.5 + _rng.NextDouble() * 8,
            Harmonic3rd = h1 * 0.25 + _rng.NextDouble() * 5,
            Harmonic4th = h1 * 0.12 + _rng.NextDouble() * 4,
            AccelX = Math.Sin(t) * 2 + (_rng.NextDouble() - 0.5),
            AccelY = Math.Cos(t) * 2 + (_rng.NextDouble() - 0.5),
            AccelZ = 9.81 + (_rng.NextDouble() - 0.5),
        };
    }
}
