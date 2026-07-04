import axios from "axios";
import {
  HubConnection,
  HubConnectionBuilder,
  LogLevel,
} from "@microsoft/signalr";
import type { Device, Reading, Stats } from "./types";

// URL da API .NET. Configurável por VITE_API_URL (ver .env).
export const API_URL =
  import.meta.env.VITE_API_URL ?? "http://localhost:5080";

const http = axios.create({ baseURL: API_URL });

export async function getDevices(): Promise<Device[]> {
  const { data } = await http.get<Device[]>("/api/devices");
  return data;
}

export async function getStats(deviceId?: string): Promise<Stats> {
  const { data } = await http.get<Stats>("/api/stats", {
    params: deviceId ? { deviceId } : {},
  });
  return data;
}

export async function getReadings(
  deviceId?: string,
  limit = 120
): Promise<Reading[]> {
  const { data } = await http.get<Reading[]>("/api/readings", {
    params: { deviceId, limit },
  });
  return data;
}

/** Abre a conexão SignalR e chama onReading a cada nova leitura. */
export function connectHub(onReading: (r: Reading) => void): HubConnection {
  const conn = new HubConnectionBuilder()
    .withUrl(`${API_URL}/hubs/vibration`)
    .withAutomaticReconnect()
    .configureLogging(LogLevel.Warning)
    .build();

  conn.on("reading", (r: Reading) => onReading(r));
  conn.start().catch((err) => console.error("SignalR falhou:", err));
  return conn;
}
