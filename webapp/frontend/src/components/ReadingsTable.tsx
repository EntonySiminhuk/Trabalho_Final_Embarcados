import type { Reading } from "../types";

interface Props {
  readings: Reading[];
}

/** Tabela das leituras mais recentes (mais nova no topo). */
export function ReadingsTable({ readings }: Props) {
  const rows = [...readings].reverse().slice(0, 12);

  return (
    <div className="card">
      <h3>Leituras recentes</h3>
      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Horário</th>
              <th>1ª</th>
              <th>2ª</th>
              <th>3ª</th>
              <th>4ª</th>
              <th>Estado</th>
            </tr>
          </thead>
          <tbody>
            {rows.length === 0 && (
              <tr>
                <td colSpan={6} className="muted">Sem dados ainda…</td>
              </tr>
            )}
            {rows.map((r) => (
              <tr key={r.id} className={r.isAnomaly ? "row--anomaly" : ""}>
                <td>{new Date(r.timestamp).toLocaleTimeString()}</td>
                <td>{r.harmonic1st.toFixed(1)}</td>
                <td>{r.harmonic2nd.toFixed(1)}</td>
                <td>{r.harmonic3rd.toFixed(1)}</td>
                <td>{r.harmonic4th.toFixed(1)}</td>
                <td>
                  {r.isAnomaly ? (
                    <span className="badge badge--bad">ANOMALIA</span>
                  ) : (
                    <span className="badge badge--ok">ok</span>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
