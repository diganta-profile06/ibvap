import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { supabase } from "../supabaseClient.js";

// NOTE: You said your dashboard UI is already built and will be attached separately.
// This is a minimal working placeholder wired to the same `detections` table so you
// can swap it out for your real dashboard component without touching the data layer —
// just reuse the queries below (or copy them into your existing dashboard file).

export default function Dashboard() {
  const [stats, setStats] = useState({ total: 0, byType: {}, recent: [] });
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  useEffect(() => {
    let cancelled = false;

    async function load() {
      setLoading(true);
      const { data, error } = await supabase
        .from("detections")
        .select("*")
        .order("created_at", { ascending: false })
        .limit(200);

      if (cancelled) return;
      if (error) {
        setError(error.message);
        setLoading(false);
        return;
      }

      const byType = {};
      for (const row of data) {
        byType[row.detection_type] = (byType[row.detection_type] || 0) + 1;
      }

      setStats({ total: data.length, byType, recent: data.slice(0, 10) });
      setLoading(false);
    }

    load();
    const interval = setInterval(load, 5000); // refresh every 5s
    return () => {
      cancelled = true;
      clearInterval(interval);
    };
  }, []);

  return (
    <div className="page">
      <div className="topbar">
        <h1>Dashboard</h1>
        <div className="nav-links">
          <Link to="/">Home</Link>
          <Link to="/ibvap-dashboard">Streaming</Link>
          <Link to="/history">History</Link>
        </div>
      </div>

      {error && (
        <div className="panel" style={{ borderColor: "var(--danger)", marginBottom: 16 }}>
          Couldn't load from Supabase: {error}. Check <code>src/supabaseClient.js</code>.
        </div>
      )}

      <div className="grid" style={{ gridTemplateColumns: "repeat(auto-fit, minmax(200px, 1fr))", marginBottom: 24 }}>
        <div className="panel">
          <div style={{ color: "var(--text-dim)", fontSize: 13 }}>Total detections (last 200)</div>
          <div style={{ fontSize: 32, fontWeight: 700 }}>{loading ? "…" : stats.total}</div>
        </div>
        {Object.entries(stats.byType).map(([type, count]) => (
          <div className="panel" key={type}>
            <div style={{ color: "var(--text-dim)", fontSize: 13, textTransform: "capitalize" }}>{type}</div>
            <div style={{ fontSize: 32, fontWeight: 700, color: "var(--accent)" }}>{count}</div>
          </div>
        ))}
      </div>

      <div className="panel">
        <h3 style={{ marginTop: 0 }}>Most recent events</h3>
        <table style={{ width: "100%", borderCollapse: "collapse" }}>
          <thead>
            <tr style={{ textAlign: "left", color: "var(--text-dim)", fontSize: 13 }}>
              <th style={{ padding: "6px 8px" }}>Time</th>
              <th style={{ padding: "6px 8px" }}>Camera</th>
              <th style={{ padding: "6px 8px" }}>Type</th>
              <th style={{ padding: "6px 8px" }}>Confidence</th>
            </tr>
          </thead>
          <tbody>
            {stats.recent.map((row) => (
              <tr key={row.id} style={{ borderTop: "1px solid var(--panel-border)" }}>
                <td style={{ padding: "6px 8px" }}>{new Date(row.created_at).toLocaleTimeString()}</td>
                <td style={{ padding: "6px 8px" }}>{row.camera_id}</td>
                <td style={{ padding: "6px 8px", textTransform: "capitalize" }}>{row.detection_type}</td>
                <td style={{ padding: "6px 8px" }}>
                  {row.confidence != null ? Number(row.confidence).toFixed(2) : "—"}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
