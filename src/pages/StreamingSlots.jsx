import { useEffect, useRef, useState } from "react";
import { Link } from "react-router-dom";
import { STREAM_SERVER_URL, CAMERA_IDS as DEFAULT_CAMERA_IDS } from "../config.js";

const POLL_INTERVAL_MS = 350;

function useLiveFeed(cameraId, canvasRef) {
  const imgRef = useRef(new Image());
  const [isLive, setIsLive] = useState(false);

  useEffect(() => {
    let cancelled = false;

    function drawFrame(detections = []) {
      const canvas = canvasRef.current;
      const img = imgRef.current;
      if (!canvas || !img.width) return;
      canvas.width = img.width;
      canvas.height = img.height;
      const ctx = canvas.getContext("2d");
      ctx.drawImage(img, 0, 0);
      ctx.lineWidth = 3;
      ctx.font = "14px 'Courier New', monospace";
      detections.forEach((d) => {
        const color = d.type === "person" ? "#c9a24b" : d.type === "face" ? "#c98a3a" : "#b3453c";
        ctx.strokeStyle = color;
        ctx.fillStyle = color;
        ctx.strokeRect(d.box.x, d.box.y, d.box.w, d.box.h);
        ctx.fillText(d.type.toUpperCase(), d.box.x, Math.max(d.box.y - 6, 12));
      });
    }

    async function poll() {
      try {
        const frameResp = await fetch(`${STREAM_SERVER_URL}/api/frames/${cameraId}/latest?t=${Date.now()}`);
        if (frameResp.ok) {
          const blob = await frameResp.blob();
          const url = URL.createObjectURL(blob);
          imgRef.current.onload = () => {
            drawFrame();
            URL.revokeObjectURL(url);
          };
          imgRef.current.src = url;
          if (!cancelled) setIsLive(true);
        } else if (!cancelled) {
          setIsLive(false);
        }
      } catch {
        if (!cancelled) setIsLive(false);
      }

      try {
        const detResp = await fetch(`${STREAM_SERVER_URL}/api/frames/${cameraId}/detections`);
        if (detResp.ok) {
          const data = await detResp.json();
          drawFrame(data.detections || []);
        }
      } catch {
        /* best-effort overlay */
      }
    }

    const interval = setInterval(poll, POLL_INTERVAL_MS);
    poll();
    return () => {
      cancelled = true;
      clearInterval(interval);
    };
  }, [cameraId, canvasRef]);

  return isLive;
}

const styles = {
  page: {
    height: "100vh",
    display: "flex",
    flexDirection: "column",
    background: "var(--bg)",
    color: "var(--text)",
    fontFamily: "'Courier New', Courier, monospace",
  },
  topbar: {
    display: "flex",
    justifyContent: "space-between",
    alignItems: "center",
    padding: "14px 20px",
    borderBottom: "1px solid var(--panel-border)",
    background: "var(--panel)",
  },
  title: {
    color: "var(--accent)",
    letterSpacing: 2,
    fontSize: 14,
    fontWeight: 700,
  },
  addBtn: {
    background: "transparent",
    border: "1px solid var(--accent-dim)",
    color: "var(--accent)",
    padding: "8px 16px",
    fontSize: 12,
    letterSpacing: 1,
    cursor: "pointer",
    fontFamily: "inherit",
  },
  body: {
    flex: 1,
    display: "grid",
    gridTemplateColumns: "220px 1fr 260px",
    overflow: "hidden",
  },
  sidebar: {
    borderRight: "1px solid var(--panel-border)",
    padding: 16,
    overflowY: "auto",
  },
  sidebarLabel: {
    color: "var(--text-dim)",
    fontSize: 11,
    letterSpacing: 2,
    marginBottom: 12,
  },
  sourceItem: (active) => ({
    padding: "8px 10px",
    fontSize: 13,
    color: active ? "#1a1206" : "var(--text-dim)",
    background: active ? "var(--accent)" : "transparent",
    cursor: "pointer",
    marginBottom: 4,
    borderRadius: 2,
  }),
  center: {
    display: "flex",
    flexDirection: "column",
    padding: 20,
    overflow: "hidden",
  },
  centerHeader: {
    color: "var(--accent)",
    fontSize: 13,
    letterSpacing: 1,
    marginBottom: 12,
    textShadow: "0 0 8px rgba(201,162,75,0.5)",
  },
  videoBox: {
    flex: 1,
    border: "1px solid var(--panel-border)",
    background: "#05070b",
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    position: "relative",
    minHeight: 0,
  },
  noVideo: {
    color: "var(--text-dim)",
    fontSize: 13,
    letterSpacing: 2,
  },
  controls: {
    display: "flex",
    gap: 10,
    marginTop: 14,
  },
  controlBtn: {
    background: "transparent",
    border: "1px solid var(--panel-border)",
    color: "var(--text-dim)",
    padding: "8px 14px",
    fontSize: 11,
    letterSpacing: 1,
    cursor: "pointer",
    fontFamily: "inherit",
  },
  meta: {
    marginTop: 14,
    color: "var(--text-dim)",
    fontSize: 12,
    lineHeight: 1.8,
  },
  infoPanel: {
    padding: 16,
    overflowY: "auto",
  },
  infoLabel: {
    color: "var(--text-dim)",
    fontSize: 11,
    letterSpacing: 2,
    marginBottom: 16,
  },
  field: { marginBottom: 16 },
  fieldLabel: { color: "var(--text-dim)", fontSize: 11, marginBottom: 3 },
  fieldValue: { color: "var(--text)", fontSize: 13 },
  statusbar: {
    borderTop: "1px solid var(--panel-border)",
    background: "var(--panel)",
    padding: "8px 20px",
    color: "var(--text-dim)",
    fontSize: 12,
  },
};

export default function StreamingSlots() {
  const [sources, setSources] = useState(DEFAULT_CAMERA_IDS);
  const [selected, setSelected] = useState(DEFAULT_CAMERA_IDS[0] || null);
  const canvasRef = useRef(null);
  const isLive = useLiveFeed(selected, canvasRef);

  function addSource() {
    const name = window.prompt("New source name (camera id):");
    if (name && !sources.includes(name)) {
      setSources((s) => [...s, name]);
      setSelected(name);
    }
  }

  function removeSource(id) {
    setSources((s) => s.filter((c) => c !== id));
    if (selected === id) setSelected(null);
  }

  return (
    <div style={styles.page}>
      <div style={styles.topbar}>
        <span style={styles.title}>IBVAP STREAMING WORKSPACE</span>
        <div style={{ display: "flex", gap: 16, alignItems: "center" }}>
          <Link to="/dashboard" style={{ color: "var(--text-dim)", fontSize: 12, textDecoration: "none" }}>DASHBOARD</Link>
          <Link to="/history" style={{ color: "var(--text-dim)", fontSize: 12, textDecoration: "none" }}>HISTORY</Link>
          <button style={styles.addBtn} onClick={addSource}>+ ADD SOURCE</button>
        </div>
      </div>

      <div style={styles.body}>
        <div style={styles.sidebar}>
          <div style={styles.sidebarLabel}>SOURCES</div>
          {sources.map((id, i) => (
            <div key={id} style={styles.sourceItem(id === selected)} onClick={() => setSelected(id)}>
              {i + 1}. {id}
            </div>
          ))}
          {sources.length === 0 && (
            <div style={{ color: "var(--text-dim)", fontSize: 12 }}>No sources yet</div>
          )}
        </div>

        <div style={styles.center}>
          <div style={styles.centerHeader}>
            {selected ? `Mobile device ${isLive ? "LIVE" : "STARTING"}` : "No source selected"}
          </div>
          <div style={styles.videoBox}>
            {selected ? (
              <>
                <canvas ref={canvasRef} style={{ maxWidth: "100%", maxHeight: "100%", display: isLive ? "block" : "none" }} />
                {!isLive && <div style={styles.noVideo}>NO VIDEO</div>}
              </>
            ) : (
              <div style={styles.noVideo}>SELECT A SOURCE</div>
            )}
          </div>
          {selected && (
            <>
              <div style={styles.controls}>
                <button style={styles.controlBtn}>{isLive ? "LIVE" : "STARTING STREAM"}</button>
                <button style={styles.controlBtn}>FULL SCREEN</button>
                <button style={styles.controlBtn} onClick={() => removeSource(selected)}>REMOVE</button>
              </div>
              <div style={styles.meta}>
                Device: {selected}<br />
                Endpoint: {STREAM_SERVER_URL}/api/frames/{selected}
              </div>
            </>
          )}
        </div>

        <div style={styles.infoPanel}>
          <div style={styles.infoLabel}>SOURCE INFO</div>
          {selected ? (
            <>
              <div style={styles.field}>
                <div style={styles.fieldLabel}>Name</div>
                <div style={styles.fieldValue}>{selected}</div>
              </div>
              <div style={styles.field}>
                <div style={styles.fieldLabel}>Type</div>
                <div style={styles.fieldValue}>Mobile device</div>
              </div>
              <div style={styles.field}>
                <div style={styles.fieldLabel}>State</div>
                <div style={styles.fieldValue}>{isLive ? "LIVE STREAM" : "STARTING STREAM"}</div>
              </div>
              <div style={styles.field}>
                <div style={styles.fieldLabel}>Connection</div>
                <div style={{ color: isLive ? "var(--accent)" : "var(--text-dim)", fontSize: 13, textShadow: isLive ? "0 0 6px rgba(201,162,75,0.6)" : "none" }}>
                  {isLive ? "Device connected" : "Waiting for device..."}
                </div>
              </div>
            </>
          ) : (
            <div style={{ color: "var(--text-dim)", fontSize: 12 }}>Select a source to see details</div>
          )}
        </div>
      </div>

      <div style={styles.statusbar}>
        Sources: {sources.length} | Local-first streaming workspace
      </div>
    </div>
  );
}
