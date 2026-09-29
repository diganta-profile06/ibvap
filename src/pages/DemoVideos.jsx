import { useNavigate } from "react-router-dom";

// Drop your video files into frontend/public/demos/ and reference them here.
const DEMO_VIDEOS = [
  { title: "VIDEO 1", src: "/demos/video1.mp4" },
  { title: "VIDEO 2", src: "/demos/video2.mp4" },
];

export default function DemoVideos() {
  const navigate = useNavigate();

  return (
    <div className="page" style={{ maxWidth: 480, margin: "0 auto", textAlign: "center", paddingTop: 60 }}>
      <h2 style={{ marginBottom: 32 }}>IBVAP DEMO VIDEOS</h2>

      <div style={{ display: "flex", flexDirection: "column", gap: 24, marginBottom: 32 }}>
        {DEMO_VIDEOS.map((v) => (
          <div key={v.src} className="panel">
            <div style={{ marginBottom: 8, color: "var(--text-dim)", fontSize: 14, fontWeight: 700 }}>
              {v.title}
            </div>
            <video controls style={{ width: "100%", borderRadius: 8, background: "#000" }}>
              <source src={v.src} />
            </video>
          </div>
        ))}
      </div>

      <button className="btn secondary" style={{ marginBottom: 16 }} onClick={() => navigate("/")}>
        BACK
      </button>
      <br />
      <button className="btn" onClick={() => navigate("/ibvap-dashboard")}>
        START IBVAP
      </button>
    </div>
  );
}
