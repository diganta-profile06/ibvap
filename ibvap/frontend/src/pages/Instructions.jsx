import { useNavigate } from "react-router-dom";

const STEPS = [
  "Start the IBVAP monitoring system.",
  "Connect the available surveillance cameras.",
  "Camera feeds are displayed in the Video Streaming Slots.",
  "OpenCV processes the incoming video.",
  "AI analyzes the video for detected objects/events.",
  "Detection information is stored in the database.",
  "The Dashboard displays current monitoring information.",
  "History Log stores previous events.",
];

export default function Instructions() {
  const navigate = useNavigate();

  return (
    <div className="page" style={{ maxWidth: 520, margin: "0 auto", paddingTop: 60 }}>
      <h2 style={{ textAlign: "center", marginBottom: 32 }}>IBVAP INSTRUCTIONS</h2>

      <div className="panel">
        <ol style={{ color: "var(--text-dim)", lineHeight: 2 }}>
          {STEPS.map((step) => (
            <li key={step}>{step}</li>
          ))}
        </ol>
      </div>

      <div style={{ display: "flex", justifyContent: "space-between", marginTop: 24 }}>
        <button className="btn secondary" onClick={() => navigate("/")}>
          BACK
        </button>
        <button className="btn" onClick={() => navigate("/ibvap-dashboard")}>
          START IBVAP
        </button>
      </div>
    </div>
  );
}
