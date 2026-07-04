import {
  Area,
  AreaChart,
  CartesianGrid,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import type { Reading } from "../types";

interface Props {
  readings: Reading[];
  threshold: number;
}

/** Série temporal da 1ª harmônica com a linha de limite crítico. */
export function HarmonicChart({ readings, threshold }: Props) {
  const data = readings.map((r) => ({
    t: new Date(r.timestamp).toLocaleTimeString(),
    h1: Number(r.harmonic1st.toFixed(2)),
    anomaly: r.isAnomaly,
  }));

  return (
    <div className="card">
      <h3>1ª harmônica no tempo</h3>
      <ResponsiveContainer width="100%" height={280}>
        <AreaChart data={data} margin={{ top: 10, right: 20, left: -10, bottom: 0 }}>
          <defs>
            <linearGradient id="h1grad" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#38bdf8" stopOpacity={0.7} />
              <stop offset="100%" stopColor="#38bdf8" stopOpacity={0.05} />
            </linearGradient>
          </defs>
          <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
          <XAxis dataKey="t" tick={{ fill: "#64748b", fontSize: 11 }} minTickGap={40} />
          <YAxis tick={{ fill: "#64748b", fontSize: 11 }} />
          <Tooltip
            contentStyle={{
              background: "#0f172a",
              border: "1px solid #1e293b",
              borderRadius: 8,
              color: "#e2e8f0",
            }}
          />
          <ReferenceLine
            y={threshold}
            stroke="#f43f5e"
            strokeDasharray="6 4"
            label={{ value: `limite ${threshold}`, fill: "#f43f5e", fontSize: 11, position: "insideTopRight" }}
          />
          <Area
            type="monotone"
            dataKey="h1"
            stroke="#38bdf8"
            strokeWidth={2}
            fill="url(#h1grad)"
            isAnimationActive={false}
          />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}
