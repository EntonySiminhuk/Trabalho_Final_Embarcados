using Microsoft.AspNetCore.SignalR;

namespace VibraMonitor.Api.Hubs;

/// <summary>
/// Hub SignalR que empurra novas leituras para os dashboards conectados,
/// em tempo real. O cliente escuta o evento "reading".
/// </summary>
public class VibrationHub : Hub
{
}
