import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { supabase } from "../supabaseClient.js";

export default function History() {
  const [rows, setRows] = useState([]);
  const [typeFilter, setTypeFilter] = useState("all");
  const [cameraFilter, setCameraFilter] = useState("all");
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  useEffect(() => {
    async function load() {
      setLoading(true);
      let query = supabase.from("detections").select("*").order("created_at", { ascending: false }).limit(500);
      const { data, error } = await query;
      if (error) setError(error.message);
      else setRows(data);
      setLoading(false);
    }
    load();
  }, []);

  const types = ["all", ...new Set(rows.map((r) => r.detection_type))];
  const cameras = ["all", ...new Set(rows.map((r) => r.camera_id))];

  const filtered = rows.filter(
    (r) =>
      (typeFilter === "all" || r.detection_type === typeFilter) &&
      (cameraFilter === "all" || r.camera_id === cameraFilter)
  );

  return (
    <div className="page">
      <div className="topbar">
        <h1>History / Log</h1>
        <div className="nav-links">
          <Link to="/">Home</Link>
          <Link to="/streaming">Streaming</Link>
          <Link to="/dashboard">Dashboard</Link>
        </div>
      </div>

      {error && (
        <div className="panel" style={{ borderColor: "var(--danger)", marginBottom: 16 }}>
          Couldn't load from Supabase: {error}. Check <code>src/supabaseClient.js</code>.
        </div>
      )}

      <div className="panel" style={{ marginBottom: 16, display: "flex", gap: 16 }}>
        <label style={{ color: "var(--text-dim)", fontSize: 14 }}>
          Type:{" "}
          <select value={typeFilter} onChange={(e) => setTypeFilter(e.target.value)}>
            {types.map((t) => (
              <option key={t} value={t}>{t}</option>
            ))}
          </select>
        </label>
        <label style={{ color: "var(--text-dim)", fontSize: 14 }}>
          Camera:{" "}
          <select value={cameraFilter} onChange={(e) => setCameraFilter(e.target.value)}>
            {cameras.map((c) => (
              <option key={c} value={c}>{c}</option>
            ))}
          </select>
        </label>
        <div style={{ marginLeft: "auto", color: "var(--text-dim)", fontSize: 14 }}>
          {filtered.length} events
        </div>
      </div>

      <div className="panel">
        {loading ? (
          <div style={{ color: "var(--text-dim)" }}>Loading…</div>
        ) : (
          <table style={{ width: "100%", borderCollapse: "collapse" }}>
            <thead>
              <tr style={{ textAlign: "left", color: "var(--text-dim)", fontSize: 13 }}>
                <th style={{ padding: "6px 8px" }}>Time</th>
                <th style={{ padding: "6px 8px" }}>Camera</th>
                <th style={{ padding: "6px 8px" }}>Type</th>
                <th style={{ padding: "6px 8px" }}>Confidence</th>
                <th style={{ padding: "6px 8px" }}>Bounding box</th>
              </tr>
            </thead>
            <tbody>
              {filtered.map((row) => (
                <tr key={row.id} style={{ borderTop: "1px solid var(--panel-border)" }}>
                  <td style={{ padding: "6px 8px" }}>{new Date(row.created_at).toLocaleString()}</td>
                  <td style={{ padding: "6px 8px" }}>{row.camera_id}</td>
                  <td style={{ padding: "6px 8px", textTransform: "capitalize" }}>{row.detection_type}</td>
                  <td style={{ padding: "6px 8px" }}>
                    {row.confidence != null ? Number(row.confidence).toFixed(2) : "—"}
                  </td>
                  <td style={{ padding: "6px 8px", color: "var(--text-dim)", fontSize: 12 }}>
                    {row.bounding_box ? JSON.stringify(row.bounding_box) : "—"}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}
