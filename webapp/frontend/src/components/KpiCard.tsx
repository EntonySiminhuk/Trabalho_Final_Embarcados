interface Props {
  label: string;
  value: string;
  hint?: string;
  tone?: "default" | "good" | "warn" | "bad";
}

export function KpiCard({ label, value, hint, tone = "default" }: Props) {
  return (
    <div className={`kpi kpi--${tone}`}>
      <span className="kpi__label">{label}</span>
      <span className="kpi__value">{value}</span>
      {hint && <span className="kpi__hint">{hint}</span>}
    </div>
  );
}
