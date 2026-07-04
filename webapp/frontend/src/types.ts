// Espelham os DTOs da API .NET (VibraMonitor.Api/Models/Dtos.cs).

export interface Reading {
  id: number;
  deviceId: string;
  timestamp: string;
  harmonicDc: number;
  harmonic1st: number;
  harmonic2nd: number;
  harmonic3rd: number;
  harmonic4th: number;
  accelX: number | null;
  accelY: number | null;
  accelZ: number | null;
  isAnomaly: boolean;
}

export interface Stats {
  deviceId: string;
  totalReadings: number;
  anomalyCount: number;
  maxHarmonic1st: number;
  avgHarmonic1st: number;
  anomalyThreshold: number;
  lastSeen: string | null;
  latest: Reading | null;
}

export interface Device {
  deviceId: string;
  totalReadings: number;
  lastSeen: string | null;
  hasRecentAnomaly: boolean;
}
