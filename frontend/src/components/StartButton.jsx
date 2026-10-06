import { useEffect, useRef, useState } from "react";
import { useNavigate } from "react-router-dom";

// START IBVAP opens the installed IBVAP desktop app.
//
// 1) Preferred: ask the local web server (npm run dev / preview) to start
//    IBVAP.exe directly - no prompt, nothing to register.
// 2) Fallback: the ibvap:// link (needs installer/register_ibvap_protocol.bat
//    once; the browser then asks "Open IBVAP?").
const DOWNLOAD_URL = "https://drive.google.com/file/d/1TDX6YEvjIGLN1q29ceMaKtY9yeWHxFLX/view?usp=drive_link";
const isLocalHost = ["localhost", "127.0.0.1", "[::1]"].includes(window.location.hostname);

export default function StartButton({ style }) {
  const navigate = useNavigate();
  const [phase, setPhase] = useState("idle"); // idle | opening | started | running | notfound | link | silent
  const leftPage = useRef(false);
  const timer = useRef(null);

  useEffect(() => {
    const mark = () => { leftPage.current = true; };
    window.addEventListener("blur", mark);
    document.addEventListener("visibilitychange", mark);
    return () => {
      window.removeEventListener("blur", mark);
      document.removeEventListener("visibilitychange", mark);
      clearTimeout(timer.current);
    };
  }, []);

  function tryLink() {
    leftPage.current = false;
    setPhase("link");
    window.location.href = "ibvap://launch";
    clearTimeout(timer.current);
    timer.current = setTimeout(() => {
      if (!leftPage.current) setPhase("silent");
    }, 8000);
  }

  async function launch() {
    setPhase("opening");
    // On the public website there is no local launcher, so go straight to ibvap://
    if (!isLocalHost) return tryLink();
    try {
      const r = await fetch("/__ibvap/launch", { method: "POST" });
      const body = await r.json().catch(() => ({}));
      if (r.ok && body.status === "started") return setPhase("started");
      if (r.ok && body.status === "already-running") return setPhase("running");
      if (r.status === 404 && body.status === "not-found") return setPhase("notfound");
    } catch { /* no local launcher on this server */ }
    tryLink();
  }

  const small = { marginTop: 10, fontSize: 12, color: "var(--text-dim)", lineHeight: 1.7, textAlign: "left" };
  const link = { color: "var(--accent)", cursor: "pointer", textDecoration: "underline" };
  const code = { fontFamily: "monospace" };

  return (
    <div style={style}>
      <button className="btn" style={{ width: "100%" }} onClick={launch}>START IBVAP</button>

      {phase === "opening" && <div style={small}>Opening IBVAP...</div>}
      {phase === "started" && <div style={small}>IBVAP is starting - its window will appear in a moment.</div>}
      {phase === "running" && <div style={small}>IBVAP is already running - brought to the front.</div>}
      {phase === "link" && (
        <div style={small}>Opening IBVAP... if your browser asks "Open IBVAP?", click <b>Open</b>.</div>
      )}

      {phase === "notfound" && (
        <div style={small}>
          <b style={{ color: "#ff6b5e" }}>Couldn't find the installed IBVAP.exe.</b> Install IBVAP first, or create a file
          {" "}<span style={code}>frontend\ibvap-path.txt</span> containing the full path to IBVAP.exe, then click START again.
        </div>
      )}

      {phase === "silent" && !isLocalHost && (
        <div style={small}>
          <b>Didn't open?</b> If your browser asked <b>"Open IBVAP?"</b>, click <b>Open</b> (tick "Always allow").
          <div style={{ marginTop: 6 }}>
            No prompt appeared? One-time fix for this PC: {" "}
            <a style={link} href={`${import.meta.env.BASE_URL}Fix_START_IBVAP.bat`} download>download Fix_START_IBVAP.bat</a>
            {" "}and double-click it (if Windows warns, click <b>More info</b> then <b>Run anyway</b>). Then click START IBVAP again.
          </div>
          <div style={{ marginTop: 6 }}>
            IBVAP not installed yet? <a style={link} href={DOWNLOAD_URL} target="_blank" rel="noreferrer">Download the installer</a>.
            Or <span style={link} onClick={() => navigate("/ibvap-dashboard")}>try the web workspace</span> in your browser.
          </div>
        </div>
      )}

      {phase === "silent" && isLocalHost && (
        <div style={small}>
          <b>Didn't open?</b> Start the web page with <span style={code}>Start_IBVAP_Web.bat</span> (it can launch IBVAP
          directly), or run <span style={code}>installer\register_ibvap_protocol.bat</span> once.
          <div style={{ marginTop: 6 }}>
            Or <span style={link} onClick={() => navigate("/ibvap-dashboard")}>use the web workspace</span> instead.
          </div>
        </div>
      )}
    </div>
  );
}
