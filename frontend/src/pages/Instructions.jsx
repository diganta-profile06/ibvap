import StartButton from "../components/StartButton.jsx";
import { useNavigate } from "react-router-dom";

const STEPS = [
  "Download IBVAP and extract the .zip file.",
  "Run IBVAP_Setup.exe. If Windows SmartScreen warns, click More info → Run anyway.",
  "Launch IBVAP from the Start menu.",
  "Choose a video source: Mobile Camera, Laptop Camera, Video File, or RTSP Stream.",
  "For Mobile Camera: turn on the laptop's Mobile Hotspot, connect the phone to it, open the address shown by IBVAP, and allow camera access.",
  "Camera feeds are displayed in the Video Streaming Slots.",
  "OpenCV + ncnn process the incoming video and the AI detects objects/events.",
  "Detection information is stored in the SQLite database.",
  "The Dashboard displays current monitoring information.",
  "History Log stores and lets you search previous events.",
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
        <StartButton style={{ minWidth: 160 }} />
      </div>
    </div>
  );
}
