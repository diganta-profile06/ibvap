import { useNavigate } from "react-router-dom";

const TEAM_NAME = "Coding Leyaks";
const TEAM_MEMBERS = ["Member 1", "Member 2", "Member 3", "Member 4"];

export default function MainPage() {
  const navigate = useNavigate();

  return (
    <div className="page" style={{ maxWidth: 480, margin: "0 auto", textAlign: "center", paddingTop: 60 }}>
      <div className="panel">
        <h1 style={{ fontSize: 40, margin: "0 0 4px 0" }}>IBVAP</h1>
        <div style={{ color: "var(--text-dim)", marginBottom: 28 }}>
          Intelligent Border Video Analysis &amp; Protection
        </div>

        <div style={{ color: "var(--accent)", fontWeight: 700, letterSpacing: 1 }}>
          TEAM: {TEAM_NAME}
        </div>

        <div style={{ marginTop: 20, marginBottom: 24 }}>
          <div style={{ color: "var(--text-dim)", fontWeight: 700, marginBottom: 8, fontSize: 13, letterSpacing: 1 }}>
            TEAM MEMBERS
          </div>
          <ul style={{ listStyle: "none", padding: 0, color: "var(--text-dim)", lineHeight: 1.8 }}>
            {TEAM_MEMBERS.map((m) => (
              <li key={m}>• {m}</li>
            ))}
          </ul>
        </div>

        <button className="btn" style={{ width: "100%", marginBottom: 20 }} onClick={() => navigate("/ibvap-dashboard")}>
          START IBVAP
        </button>

        <div style={{ display: "flex", justifyContent: "center", gap: 32 }}>
          <span style={{ color: "var(--accent)", cursor: "pointer" }} onClick={() => navigate("/instructions")}>
            Instructions
          </span>
          <span style={{ color: "var(--accent)", cursor: "pointer" }} onClick={() => navigate("/demo-videos")}>
            Demo Videos
          </span>
        </div>
      </div>
    </div>
  );
}
