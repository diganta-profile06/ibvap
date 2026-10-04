import StartButton from "../components/StartButton.jsx";
import { useNavigate } from "react-router-dom";

// Google Drive demo videos. `id` is the file id from the share link
// (https://drive.google.com/file/d/<id>/view). Each file must be shared as
// "Anyone with the link" for the embedded player to work.
const DEMO_VIDEOS = [
  { title: "THERMAL DETECTION", id: "1ZnBVc7YIK7K_A7B3Q3OWrDvuamqsMsZR" },
  { title: "FACE RECOGNITION", id: "1GHHAvQMsN6qovOwz2C2bA_sVjWvBc6cL" },
  { title: "VIRTUAL FENCE", id: "1aKR4i98EcWHFGDycz9wFBtqOIyVHYy4a" },
  { title: "VEHICLE DETECTION", id: "1ro6woOqg_Xr2_u6MWc0cgZsf47cshHjm" },
];

export default function DemoVideos() {
  const navigate = useNavigate();

  return (
    <div className="page" style={{ maxWidth: 1100, margin: "0 auto", textAlign: "center", paddingTop: 60 }}>
      <h2 style={{ marginBottom: 32 }}>IBVAP DEMO VIDEOS</h2>

      <div
        style={{
          display: "grid",
          gridTemplateColumns: "repeat(auto-fit, minmax(min(100%, 440px), 1fr))",
          gap: 24,
          marginBottom: 32,
        }}
      >
        {DEMO_VIDEOS.map((v) => (
          <div key={v.id} className="panel">
            <div style={{ marginBottom: 8, color: "var(--text-dim)", fontSize: 14, fontWeight: 700 }}>
              {v.title}
            </div>
            <div style={{ position: "relative", paddingTop: "56.25%", borderRadius: 8, overflow: "hidden", background: "#000" }}>
              <iframe
                title={v.title}
                src={`https://drive.google.com/file/d/${v.id}/preview`}
                allow="autoplay; fullscreen"
                allowFullScreen
                style={{ position: "absolute", inset: 0, width: "100%", height: "100%", border: 0 }}
              />
            </div>
            <a
              href={`https://drive.google.com/file/d/${v.id}/view`}
              target="_blank"
              rel="noreferrer"
              style={{ display: "inline-block", marginTop: 8, fontSize: 12, color: "var(--accent)" }}
            >
              Open in Google Drive
            </a>
          </div>
        ))}
      </div>

      <button className="btn secondary" style={{ marginBottom: 16 }} onClick={() => navigate("/")}>
        BACK
      </button>
      <br />
      <StartButton style={{ display: "inline-block", minWidth: 200 }} />
    </div>
  );
}
