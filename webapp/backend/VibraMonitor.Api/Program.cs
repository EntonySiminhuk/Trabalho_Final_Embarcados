using Microsoft.EntityFrameworkCore;
using Scalar.AspNetCore;
using VibraMonitor.Api.Data;
using VibraMonitor.Api.Hubs;
using VibraMonitor.Api.Models;
using VibraMonitor.Api.Services;

var builder = WebApplication.CreateBuilder(args);

// ---- Banco de dados (PostgreSQL via EF Core) ----
builder.Services.AddDbContext<AppDbContext>(opt =>
    opt.UseNpgsql(builder.Configuration.GetConnectionString("Postgres")));

// ---- Opções ----
builder.Services.Configure<MonitoringOptions>(
    builder.Configuration.GetSection(MonitoringOptions.Section));
builder.Services.Configure<SimulatorOptions>(
    builder.Configuration.GetSection(SimulatorOptions.Section));

// ---- Serviços da aplicação ----
builder.Services.AddScoped<IngestionService>();
builder.Services.AddHostedService<SimulatorService>();

// ---- SignalR (tempo real) ----
builder.Services.AddSignalR();

// ---- OpenAPI / Scalar ----
builder.Services.AddOpenApi();

// ---- CORS para o dashboard React (dev) ----
const string CorsPolicy = "dashboard";
builder.Services.AddCors(o => o.AddPolicy(CorsPolicy, p => p
    .SetIsOriginAllowed(_ => true) // dev: aceita qualquer origem local
    .AllowAnyHeader()
    .AllowAnyMethod()
    .AllowCredentials()));

var app = builder.Build();

// ---- Aplica migrations no startup (cria o schema se não existir) ----
using (var scope = app.Services.CreateScope())
{
    var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();
    db.Database.Migrate();
}

app.UseCors(CorsPolicy);

app.MapOpenApi();
app.MapScalarApiReference(); // UI em /scalar

// ===================================================================
//  Endpoints
// ===================================================================
var api = app.MapGroup("/api");

// --- Ingestão (chamada pelo ESP32 / simulador) ---
api.MapPost("/readings", async (IngestDto dto, IngestionService svc, CancellationToken ct) =>
{
    var created = await svc.IngestAsync(dto, ct);
    return Results.Created($"/api/readings/{created.Id}", created);
})
.WithName("IngestReading")
.WithSummary("Recebe uma leitura de vibração e a persiste.");

// --- Ingestão em lote ---
api.MapPost("/readings/batch", async (IEnumerable<IngestDto> batch, IngestionService svc, CancellationToken ct) =>
{
    var results = new List<ReadingDto>();
    foreach (var dto in batch)
    {
        results.Add(await svc.IngestAsync(dto, ct));
    }
    return Results.Ok(new { count = results.Count, items = results });
})
.WithName("IngestBatch")
.WithSummary("Recebe várias leituras de uma vez.");

// --- Série temporal (para os gráficos) ---
api.MapGet("/readings", async (
    AppDbContext db,
    string? deviceId,
    DateTimeOffset? from,
    DateTimeOffset? to,
    int? limit) =>
{
    var q = db.Readings.AsNoTracking();

    if (!string.IsNullOrWhiteSpace(deviceId))
        q = q.Where(r => r.DeviceId == deviceId);
    if (from is not null)
        q = q.Where(r => r.Timestamp >= from);
    if (to is not null)
        q = q.Where(r => r.Timestamp <= to);

    var take = Math.Clamp(limit ?? 200, 1, 5000);

    // Pega as N mais recentes e devolve em ordem crescente (bom p/ gráfico).
    var rows = await q.OrderByDescending(r => r.Timestamp)
        .Take(take)
        .ToListAsync();

    rows.Reverse();
    return Results.Ok(rows.Select(ReadingDto.From));
})
.WithName("GetReadings")
.WithSummary("Lista leituras (série temporal) com filtros opcionais.");

// --- Última leitura ---
api.MapGet("/readings/latest", async (AppDbContext db, string? deviceId) =>
{
    var q = db.Readings.AsNoTracking();
    if (!string.IsNullOrWhiteSpace(deviceId))
        q = q.Where(r => r.DeviceId == deviceId);

    var latest = await q.OrderByDescending(r => r.Timestamp).FirstOrDefaultAsync();
    return latest is null ? Results.NoContent() : Results.Ok(ReadingDto.From(latest));
})
.WithName("GetLatest")
.WithSummary("Retorna a leitura mais recente.");

// --- Estatísticas agregadas ---
api.MapGet("/stats", async (AppDbContext db, string? deviceId, Microsoft.Extensions.Options.IOptions<MonitoringOptions> mon) =>
{
    var q = db.Readings.AsNoTracking();
    if (!string.IsNullOrWhiteSpace(deviceId))
        q = q.Where(r => r.DeviceId == deviceId);

    var total = await q.LongCountAsync();
    if (total == 0)
    {
        return Results.Ok(new StatsDto(
            deviceId ?? "all", 0, 0, 0, 0, mon.Value.AnomalyThreshold, null, null));
    }

    var anomalies = await q.LongCountAsync(r => r.IsAnomaly);
    var max = await q.MaxAsync(r => r.Harmonic1st);
    var avg = await q.AverageAsync(r => r.Harmonic1st);
    var latestEntity = await q.OrderByDescending(r => r.Timestamp).FirstAsync();

    return Results.Ok(new StatsDto(
        deviceId ?? "all",
        total,
        anomalies,
        max,
        avg,
        mon.Value.AnomalyThreshold,
        latestEntity.Timestamp,
        ReadingDto.From(latestEntity)));
})
.WithName("GetStats")
.WithSummary("Estatísticas agregadas (total, anomalias, máx/média da 1ª harmônica).");

// --- Lista de dispositivos ---
api.MapGet("/devices", async (AppDbContext db) =>
{
    var since = DateTimeOffset.UtcNow.AddMinutes(-5);

    var devices = await db.Readings.AsNoTracking()
        .GroupBy(r => r.DeviceId)
        .Select(g => new DeviceDto(
            g.Key,
            g.LongCount(),
            g.Max(r => r.Timestamp),
            g.Any(r => r.IsAnomaly && r.Timestamp >= since)))
        .ToListAsync();

    return Results.Ok(devices);
})
.WithName("GetDevices")
.WithSummary("Lista os dispositivos que já enviaram dados.");

app.MapHub<VibrationHub>("/hubs/vibration");

app.MapGet("/health", () => Results.Ok(new { status = "ok", time = DateTimeOffset.UtcNow }));

app.Run();
