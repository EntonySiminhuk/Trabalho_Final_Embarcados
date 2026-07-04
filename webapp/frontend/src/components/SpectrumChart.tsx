import {
  Bar,
  BarChart,
  CartesianGrid,
  Cell,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import type { Reading } from "../types";

interface Props {
  latest: Reading | null;
  threshold: number;
}

/** Espectro atual: barras das harmônicas DC..4ª da leitura mais recente. */
export function SpectrumChart({ latest, threshold }: Props) {
  const data = latest
    ? [
        { name: "DC", v: latest.harmonicDc },
        { name: "1ª", v: latest.harmonic1st },
        { name: "2ª", v: latest.harmonic2nd },
        { name: "3ª", v: latest.harmonic3rd },
        { name: "4ª", v: latest.harmonic4th },
      ].map((d) => ({ ...d, v: Number(d.v.toFixed(2)) }))
    : [];

  return (
    <div className="card">
      <h3>Espectro atual (harmônicas)</h3>
      <ResponsiveContainer width="100%" height={280}>
        <BarChart data={data} margin={{ top: 10, right: 20, left: -10, bottom: 0 }}>
          <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" vertical={false} />
          <XAxis dataKey="name" tick={{ fill: "#94a3b8", fontSize: 12 }} />
          <YAxis tick={{ fill: "#64748b", fontSize: 11 }} />
          <Tooltip
            cursor={{ fill: "#1e293b55" }}
            contentStyle={{
              background: "#0f172a",
              border: "1px solid #1e293b",
              borderRadius: 8,
              color: "#e2e8f0",
            }}
          />
          <Bar dataKey="v" radius={[4, 4, 0, 0]} isAnimationActive={false}>
            {data.map((d, i) => (
              <Cell
                key={i}
                fill={d.name === "1ª" && d.v > threshold ? "#f43f5e" : "#38bdf8"}
              />
            ))}
          </Bar>
        </BarChart>
      </ResponsiveContainer>
    </div>
  );
}
