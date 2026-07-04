import { useEffect, useRef, useState } from "react";
import "./App.css";
import { connectHub, getDevices, getReadings, getStats } from "./api";
import type { Device, Reading, Stats } from "./types";
import { KpiCard } from "./components/KpiCard";
import { HarmonicChart } from "./components/HarmonicChart";
import { SpectrumChart } from "./components/SpectrumChart";
import { ReadingsTable } from "./components/ReadingsTable";

const MAX_POINTS = 120;

export default function App() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [selected, setSelected] = useState<string>("");
  const [readings, setReadings] = useState<Reading[]>([]);
  const [stats, setStats] = useState<Stats | null>(null);
  const [connected, setConnected] = useState(false);

  const selectedRef = useRef(selected);
  selectedRef.current = selected;

  // Carga inicial: lista de dispositivos.
  useEffect(() => {
    getDevices().then((d) => {
      setDevices(d);
      if (d.length && !selectedRef.current) setSelected(d[0].deviceId);
    });
  }, []);

  // Ao trocar de dispositivo: recarrega histórico + stats.
  useEffect(() => {
    if (!selected) return;
    getReadings(selected, MAX_POINTS).then(setReadings);
    getStats(selected).then(setStats);
  }, [selected]);

  // Tempo real: uma conexão SignalR para toda a vida do app.
  useEffect(() => {
    const conn = connectHub((r) => {
      if (r.deviceId !== selectedRef.current) return;
      setReadings((prev) => [...prev, r].slice(-MAX_POINTS));
      // Atualiza stats de forma incremental (barato) sem refetch a cada leitura.
      setStats((prev) =>
        prev
          ? {
              ...prev,
              totalReadings: prev.totalReadings + 1,
              anomalyCount: prev.anomalyCount + (r.isAnomaly ? 1 : 0),
              maxHarmonic1st: Math.max(prev.maxHarmonic1st, r.harmonic1st),
              lastSeen: r.timestamp,
              latest: r,
            }
          : prev
      );
    });
    conn.onreconnected(() => setConnected(true));
    conn.onclose(() => setConnected(false));
    const iv = setInterval(() => setConnected(conn.state === "Connected"), 1500);

    // Atualiza a lista de dispositivos periodicamente (novos aparecem sozinhos).
    const devIv = setInterval(() => getDevices().then(setDevices), 5000);

    return () => {
      clearInterval(iv);
      clearInterval(devIv);
      conn.stop();
    };
  }, []);

  const threshold = stats?.anomalyThreshold ?? 100;
  const latest = stats?.latest ?? readings[readings.length - 1] ?? null;
  const isAlarm = !!latest?.isAnomaly;

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="brand__dot" />
          <div>
            <h1>VibraMonitor</h1>
            <p>Monitoramento de vibração em tempo real</p>
          </div>
        </div>

        <div className="topbar__right">
          <label className="device-select">
            Dispositivo
            <select value={selected} onChange={(e) => setSelected(e.target.value)}>
              {devices.length === 0 && <option value="">(nenhum)</option>}
              {devices.map((d) => (
                <option key={d.deviceId} value={d.deviceId}>
                  {d.deviceId} · {d.totalReadings} leituras
                </option>
              ))}
            </select>
          </label>

          <span className={`conn ${connected ? "conn--on" : "conn--off"}`}>
            <span className="conn__dot" />
            {connected ? "tempo real" : "offline"}
          </span>
        </div>
      </header>

      <section className={`banner ${isAlarm ? "banner--alarm" : "banner--ok"}`}>
        {isAlarm
          ? "⚠ ANOMALIA — 1ª harmônica acima do limite crítico"
          : "✓ Operação normal"}
      </section>

      <section className="kpis">
        <KpiCard
          label="1ª harmônica (atual)"
          value={latest ? latest.harmonic1st.toFixed(1) : "—"}
          hint={`limite ${threshold}`}
          tone={isAlarm ? "bad" : "good"}
        />
        <KpiCard
          label="Total de leituras"
          value={stats ? stats.totalReadings.toLocaleString("pt-BR") : "—"}
        />
        <KpiCard
          label="Anomalias"
          value={stats ? stats.anomalyCount.toLocaleString("pt-BR") : "—"}
          tone={stats && stats.anomalyCount > 0 ? "warn" : "default"}
        />
        <KpiCard
          label="Máx / Média (1ª)"
          value={
            stats
              ? `${stats.maxHarmonic1st.toFixed(0)} / ${stats.avgHarmonic1st.toFixed(0)}`
              : "—"
          }
        />
      </section>

      <section className="charts">
        <HarmonicChart readings={readings} threshold={threshold} />
        <SpectrumChart latest={latest} threshold={threshold} />
      </section>

      <ReadingsTable readings={readings} />

      <footer className="foot">
        API: {import.meta.env.VITE_API_URL ?? "http://localhost:5080"} · última
        atualização {latest ? new Date(latest.timestamp).toLocaleTimeString() : "—"}
      </footer>
    </div>
  );
}
