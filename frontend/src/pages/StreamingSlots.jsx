import { useEffect, useRef, useState } from "react";
import { Link } from "react-router-dom";
import { IBVAP_URL } from "../config.js";
import { useIbvapFeed } from "./useIbvapFeed.js";
import { useDetector } from "./useDetector.js";

// Same workflow as the IBVAP desktop app: + ADD SOURCE opens an
// "Add Video Source" window (name + type), CREATE SLOT adds a slot, and each
// slot type behaves like its desktop counterpart.

const STORAGE_KEY = "ibvap.slots";

const TYPES = [
  { value: "rtsp", label: "RTSP Stream" },
  { value: "laptop", label: "Laptop Camera" },
  { value: "video", label: "Pre-recorded video" },
  { value: "mobile", label: "Mobile Camera" },
];
const TYPE_LABEL = {
  rtsp: "RTSP stream",
  laptop: "Laptop camera",
  video: "Pre-recorded video",
  mobile: "Mobile device",
};
const HEADER_LABEL = {
  rtsp: "RTSP stream",
  laptop: "Laptop camera",
  video: "Pre-recorded video",
  mobile: "Mobile device",
};

const C = {
  bg: "#04080b",
  panel: "#070d11",
  border: "#17484d",
  borderDim: "#102a2e",
  accent: "#3ec9d6",
  text: "#cfe3e6",
  dim: "#6f8a8e",
  ok: "#2fd6a0",
  err: "#ff6b5e",
  btnBg: "#0b1a1d",
  input: "#0b1417",
};

const S = {
  page: { height: "100vh", display: "flex", flexDirection: "column", background: C.bg, color: C.text, fontFamily: "'Courier New', Courier, monospace", padding: 8, gap: 8 },
  topbar: { display: "flex", justifyContent: "space-between", alignItems: "center", padding: "6px 10px", border: `1px solid ${C.borderDim}` },
  title: { color: C.accent, letterSpacing: 1, fontSize: 11 },
  navLink: { color: C.dim, fontSize: 11, textDecoration: "none", letterSpacing: 1 },
  addBtn: { background: C.btnBg, border: `1px solid ${C.border}`, color: C.accent, padding: "7px 18px", fontSize: 11, letterSpacing: 1, cursor: "pointer", fontFamily: "inherit", borderRadius: 3 },
  body: { flex: 1, display: "grid", gridTemplateColumns: "210px minmax(0, 1fr) 240px", gap: 8, minHeight: 0 },
  panel: { border: `1px solid ${C.border}`, background: C.panel, padding: 12, overflowY: "auto", minHeight: 0 },
  label: { color: C.accent, fontSize: 10, letterSpacing: 1, marginBottom: 10 },
  item: (active) => ({ padding: "6px 8px", fontSize: 12, color: active ? C.accent : C.dim, background: active ? "#0d2a2e" : "transparent", cursor: "pointer", marginBottom: 2 }),
  center: { display: "flex", flexDirection: "column", minWidth: 0 },
  box: { flex: 1, border: `1px solid ${C.border}`, background: "#05090c", display: "flex", alignItems: "center", justifyContent: "center", minHeight: 0, marginBottom: 10, position: "relative", overflow: "hidden" },
  hint: { color: C.dim, fontSize: 10, letterSpacing: 1, textAlign: "center", padding: 12, lineHeight: 1.7 },
  controls: { display: "flex", gap: 6, marginBottom: 10, flexWrap: "wrap" },
  btn: (primary) => ({ background: C.btnBg, border: `1px solid ${C.borderDim}`, color: primary ? C.accent : C.text, padding: "5px 9px", fontSize: 10, letterSpacing: 1, cursor: "pointer", fontFamily: "inherit", borderRadius: 3 }),
  meta: { color: C.dim, fontSize: 11, lineHeight: 1.7 },
  field: { marginBottom: 12 },
  fl: { color: C.text, fontSize: 11, marginBottom: 2 },
  fv: { color: C.dim, fontSize: 11, wordBreak: "break-all" },
  status: { border: `1px solid ${C.borderDim}`, padding: "5px 10px", color: C.dim, fontSize: 10 },
};

function loadSlots() {
  try {
    const cur = JSON.parse(localStorage.getItem(STORAGE_KEY) || "null");
    if (Array.isArray(cur)) return cur;
    // older version stored plain phone names
    const old = JSON.parse(localStorage.getItem("ibvap.sources") || "[]");
    return Array.isArray(old) ? old.map((n) => ({ id: n, name: n, type: "mobile" })) : [];
  } catch {
    return [];
  }
}

/* ------------------------------------------------------------------ */
/* Add Video Source window (same fields as the desktop app)           */
/* ------------------------------------------------------------------ */

function AddSourceDialog({ existing, onCreate, onCancel }) {
  const [name, setName] = useState("");
  const [type, setType] = useState("rtsp");
  const [url, setUrl] = useState("");
  const [error, setError] = useState("");

  function submit() {
    const clean = name.trim();
    if (!clean) return setError("Enter a source name.");
    if (existing.some((n) => n.toLowerCase() === clean.toLowerCase()))
      return setError("A source with this name already exists.");
    if (type === "rtsp" && !url.trim()) return setError("Enter the RTSP URL.");
    onCreate({ id: `${Date.now()}`, name: clean, type, url: type === "rtsp" ? url.trim() : "" });
  }

  const input = { width: "100%", boxSizing: "border-box", background: C.input, border: `1px solid ${C.border}`, color: C.text, padding: "8px 10px", fontFamily: "inherit", fontSize: 12, borderRadius: 3, outline: "none" };
  const lab = { color: C.text, fontSize: 12, margin: "12px 0 4px" };

  return (
    <div style={{ position: "fixed", inset: 0, background: "rgba(0,0,0,0.55)", display: "flex", alignItems: "center", justifyContent: "center", zIndex: 10 }}
         onKeyDown={(e) => e.key === "Escape" && onCancel()}>
      <div style={{ width: 400, maxWidth: "92vw", background: "#050c0f", border: `1px solid ${C.border}`, fontFamily: "'Courier New', monospace", color: C.text }}>
        <div style={{ padding: "6px 10px", fontSize: 12, background: "#030809", borderBottom: `1px solid ${C.borderDim}` }}>Add Video Source</div>
        <div style={{ padding: 14 }}>
          <div style={{ color: C.accent, fontSize: 12, paddingBottom: 8, borderBottom: `1px solid ${C.borderDim}` }}>ADD VIDEO SOURCE</div>

          <div style={lab}>Source Name</div>
          <input autoFocus style={input} value={name} onChange={(e) => { setName(e.target.value); setError(""); }}
                 onKeyDown={(e) => e.key === "Enter" && submit()} />

          <div style={lab}>Source Type</div>
          <select style={input} value={type} onChange={(e) => { setType(e.target.value); setError(""); }}>
            {TYPES.map((t) => <option key={t.value} value={t.value}>{t.label}</option>)}
          </select>

          <div style={{ borderTop: `1px solid ${C.borderDim}`, marginTop: 14 }} />

          {type === "rtsp" && (
            <>
              <div style={lab}>RTSP URL</div>
              <input style={input} value={url} placeholder="rtsp://user:pass@ip:554/stream"
                     onChange={(e) => { setUrl(e.target.value); setError(""); }} onKeyDown={(e) => e.key === "Enter" && submit()} />
              <div style={{ ...S.meta, marginTop: 6 }}>Browsers cannot play RTSP. The slot keeps the URL; view it in the IBVAP desktop app.</div>
            </>
          )}
          {type === "video" && <div style={{ ...S.meta, marginTop: 12 }}>After creating the slot, use SELECT VIDEO FROM DEVICE to choose a file.</div>}
          {type === "laptop" && <div style={{ ...S.meta, marginTop: 12 }}>The browser will ask permission to use this PC's camera.</div>}
          {type === "mobile" && (
            <div style={{ ...S.meta, marginTop: 12 }}>
              Use this exact name on the phone:<br />
              <span style={{ color: C.text }}>https://&lt;pc-ip&gt;:8443/mobile?source={name.trim() || "NAME"}</span><br />
              (the QR code in the IBVAP window shows the real address)
            </div>
          )}

          {error && <div style={{ color: C.err, fontSize: 11, marginTop: 10 }}>{error}</div>}

          <div style={{ display: "flex", gap: 8, marginTop: 16 }}>
            <button style={S.btn(true)} onClick={submit}>CREATE SLOT</button>
            <button style={S.btn(false)} onClick={onCancel}>CANCEL</button>
          </div>
        </div>
      </div>
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Slot views - one per source type                                   */
/* ------------------------------------------------------------------ */

function goFullScreen(el) {
  if (el && el.requestFullscreen) el.requestFullscreen();
}

// Fits the media into the available box and lays the AI overlay on top of it.
function Stage({ natural, holderRef, overlayRef, emptyText, children }) {
  const [box, setBox] = useState({ w: 0, h: 0 });

  useEffect(() => {
    const el = holderRef.current;
    if (!el) return undefined;
    const ro = new ResizeObserver(([e]) => setBox({ w: e.contentRect.width, h: e.contentRect.height }));
    ro.observe(el);
    return () => ro.disconnect();
  }, [holderRef]);

  let w = 0;
  let h = 0;
  if (natural && box.w && box.h) {
    const sc = Math.min(box.w / natural.w, box.h / natural.h);
    w = Math.floor(natural.w * sc);
    h = Math.floor(natural.h * sc);
  }

  return (
    <div ref={holderRef} style={S.box}>
      <div style={{ position: "relative", width: w, height: h, display: natural ? "block" : "none" }}>
        {children}
        <canvas ref={overlayRef} style={{ position: "absolute", inset: 0, width: "100%", height: "100%", pointerEvents: "none" }} />
      </div>
      {!natural && <div style={S.hint}>{emptyText}</div>}
    </div>
  );
}

const MEDIA = { width: "100%", height: "100%", display: "block" };

function aiInfo(ai, det) {
  const text = !ai ? "Off" : det.status === "loading" ? "Loading model..." : det.status === "error" ? "Failed to load" : "Active";
  return [
    ["AI Detection", text],
    ["Persons", String(det.counts.PERSON)],
    ["Vehicles", String(det.counts.VEHICLE)],
    ["Faces", String(det.counts.FACE)],
  ];
}

function AiButton({ ai, setAi }) {
  return (
    <button style={S.btn(ai)} onClick={() => setAi(!ai)}>
      AI DETECTION: {ai ? "ON" : "OFF"}
    </button>
  );
}

function MobileView({ slot, onInfo, onRemove }) {
  const canvasRef = useRef(null);
  const overlayRef = useRef(null);
  const holderRef = useRef(null);
  const [ai, setAi] = useState(true);
  const { status, size } = useIbvapFeed(slot.name, canvasRef);
  const live = status === "live";
  const det = useDetector(canvasRef, overlayRef, ai && live);

  useEffect(() => {
    onInfo({
      header: live ? "LIVE" : "STARTING",
      state: live ? "LIVE STREAM" : "STARTING STREAM",
      extra: [...(size ? [["Resolution", `${size.w} x ${size.h}`]] : []), ...aiInfo(ai, det)],
      connection: live ? "Device connected" : status === "error" ? "Cannot reach IBVAP" : "Waiting for device...",
      connColor: live ? C.ok : status === "error" ? C.err : C.dim,
      log: det.log,
    });
  }, [status, size, live, ai, det.status, det.counts, det.log, onInfo]);

  return (
    <>
      <Stage natural={live && size ? size : null} holderRef={holderRef} overlayRef={overlayRef} emptyText="NO VIDEO">
        <canvas ref={canvasRef} style={MEDIA} />
      </Stage>
      <div style={S.controls}>
        <button style={S.btn(true)}>{live ? "LIVE" : "STARTING STREAM"}</button>
        <AiButton ai={ai} setAi={setAi} />
        <button style={S.btn(false)} onClick={() => goFullScreen(holderRef.current)}>FULL SCREEN</button>
        <button style={S.btn(false)} onClick={onRemove}>REMOVE</button>
      </div>
      <div style={S.meta}>
        Device: {slot.name}<br />
        {status === "error" && (
          <span style={{ color: C.err }}>
            Can't reach IBVAP. Start the IBVAP app, then open {IBVAP_URL} once and accept the certificate.
          </span>
        )}
        {typeof VideoDecoder === "undefined" && (
          <span style={{ color: C.err }}>This browser has no video decoder. Use Chrome or Edge on http://localhost.</span>
        )}
      </div>
    </>
  );
}

function VideoView({ slot, onInfo, onRemove }) {
  const holderRef = useRef(null);
  const videoRef = useRef(null);
  const overlayRef = useRef(null);
  const fileRef = useRef(null);
  const [url, setUrl] = useState(null);
  const [fileName, setFileName] = useState("");
  const [natural, setNatural] = useState(null);
  const [ai, setAi] = useState(true);
  const det = useDetector(videoRef, overlayRef, ai && !!url);

  useEffect(() => () => { if (url) URL.revokeObjectURL(url); }, [url]);

  useEffect(() => {
    onInfo({
      header: url ? "LIVE" : "NO FILE",
      state: url ? "LIVE" : "NO VIDEO SELECTED",
      extra: [["File", fileName || "-"], ...aiInfo(ai, det)],
      connection: url ? "Playing from this device" : "Select a video file",
      connColor: url ? C.ok : C.dim,
      log: det.log,
    });
  }, [url, fileName, ai, det.status, det.counts, det.log, onInfo]);

  function pick(e) {
    const f = e.target.files && e.target.files[0];
    if (!f) return;
    setNatural(null);
    setUrl(URL.createObjectURL(f));
    setFileName(f.name);
    e.target.value = "";
  }

  function replay() {
    const v = videoRef.current;
    if (!v) return;
    v.currentTime = 0;
    v.play().catch(() => {});
  }

  return (
    <>
      <Stage natural={url ? natural : null} holderRef={holderRef} overlayRef={overlayRef} emptyText={url ? "LOADING..." : "NO VIDEO - use SELECT VIDEO FROM DEVICE"}>
        {url && (
          <video
            ref={videoRef}
            src={url}
            autoPlay
            muted
            loop
            playsInline
            style={MEDIA}
            onLoadedMetadata={(e) => setNatural({ w: e.target.videoWidth, h: e.target.videoHeight })}
          />
        )}
      </Stage>
      <div style={S.controls}>
        <input ref={fileRef} type="file" accept="video/*" style={{ display: "none" }} onChange={pick} />
        <button style={S.btn(true)} onClick={() => fileRef.current && fileRef.current.click()}>SELECT VIDEO FROM DEVICE</button>
        <button style={S.btn(false)} onClick={replay}>REPLAY</button>
        <AiButton ai={ai} setAi={setAi} />
        <button style={S.btn(false)} onClick={() => goFullScreen(holderRef.current)}>FULL SCREEN</button>
        <button style={S.btn(false)} onClick={onRemove}>REMOVE</button>
      </div>
      <div style={S.meta}>Device: {slot.name}<br />{fileName ? `File: ${fileName}` : "Address: not assigned"}</div>
    </>
  );
}

function LaptopView({ slot, onInfo, onRemove }) {
  const holderRef = useRef(null);
  const videoRef = useRef(null);
  const overlayRef = useRef(null);
  const [state, setState] = useState("starting"); // starting | live | error
  const [natural, setNatural] = useState(null);
  const [ai, setAi] = useState(true);
  const det = useDetector(videoRef, overlayRef, ai && state === "live");

  useEffect(() => {
    let stream = null;
    let cancelled = false;
    async function start() {
      try {
        stream = await navigator.mediaDevices.getUserMedia({ video: true, audio: false });
        if (cancelled) { stream.getTracks().forEach((t) => t.stop()); return; }
        if (videoRef.current) videoRef.current.srcObject = stream;
        setState("live");
      } catch {
        if (!cancelled) setState("error");
      }
    }
    start();
    return () => { cancelled = true; if (stream) stream.getTracks().forEach((t) => t.stop()); };
  }, []);

  useEffect(() => {
    onInfo({
      header: state === "live" ? "LIVE" : "STARTING",
      state: state === "live" ? "LIVE STREAM" : state === "error" ? "CAMERA UNAVAILABLE" : "STARTING STREAM",
      extra: aiInfo(ai, det),
      connection: state === "live" ? "Camera connected" : state === "error" ? "Permission denied or no camera" : "Waiting for camera...",
      connColor: state === "live" ? C.ok : state === "error" ? C.err : C.dim,
      log: det.log,
    });
  }, [state, ai, det.status, det.counts, det.log, onInfo]);

  return (
    <>
      <Stage natural={state === "live" ? natural : null} holderRef={holderRef} overlayRef={overlayRef} emptyText={state === "error" ? "CAMERA BLOCKED OR NOT FOUND" : "NO VIDEO"}>
        <video
          ref={videoRef}
          autoPlay
          muted
          playsInline
          style={MEDIA}
          onLoadedMetadata={(e) => setNatural({ w: e.target.videoWidth, h: e.target.videoHeight })}
        />
      </Stage>
      <div style={S.controls}>
        <button style={S.btn(true)}>{state === "live" ? "LIVE" : "STARTING STREAM"}</button>
        <AiButton ai={ai} setAi={setAi} />
        <button style={S.btn(false)} onClick={() => goFullScreen(holderRef.current)}>FULL SCREEN</button>
        <button style={S.btn(false)} onClick={onRemove}>REMOVE</button>
      </div>
      <div style={S.meta}>Device: {slot.name}</div>
    </>
  );
}

function RtspView({ slot, onInfo, onRemove }) {
  useEffect(() => {
    onInfo({
      header: "SAVED",
      state: "OPEN IN IBVAP APP",
      extra: [["URL", slot.url || "-"]],
      connection: "Browsers can't play RTSP",
      connColor: C.dim,
      log: [],
    });
  }, [slot.url, onInfo]);

  return (
    <>
      <div style={S.box}>
        <div style={S.hint}>RTSP CAN'T BE PLAYED IN A BROWSER<br />Add this URL as an RTSP source in the IBVAP desktop app</div>
      </div>
      <div style={S.controls}>
        <button style={S.btn(false)} onClick={onRemove}>REMOVE</button>
      </div>
      <div style={S.meta}>Device: {slot.name}<br />Address: {slot.url}</div>
    </>
  );
}

/* ------------------------------------------------------------------ */

export default function StreamingSlots() {
  const [slots, setSlots] = useState(loadSlots);
  const [selectedId, setSelectedId] = useState(() => (loadSlots()[0] || {}).id || null);
  const [showAdd, setShowAdd] = useState(false);
  const [info, setInfo] = useState(null);

  useEffect(() => {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(slots)); } catch { /* ignore */ }
  }, [slots]);

  const selected = slots.find((s) => s.id === selectedId) || null;

  function create(slot) {
    setSlots((s) => [...s, slot]);
    setSelectedId(slot.id);
    setInfo(null);
    setShowAdd(false);
  }

  function remove(id) {
    setSlots((s) => s.filter((x) => x.id !== id));
    if (selectedId === id) { setSelectedId(null); setInfo(null); }
  }

  const View = selected ? { mobile: MobileView, video: VideoView, laptop: LaptopView, rtsp: RtspView }[selected.type] : null;

  return (
    <div style={S.page}>
      <div style={S.topbar}>
        <span style={S.title}>IBVAP STREAMING WORKSPACE</span>
        <div style={{ display: "flex", gap: 16, alignItems: "center" }}>
          <Link to="/dashboard" style={S.navLink}>DASHBOARD</Link>
          <Link to="/history" style={S.navLink}>HISTORY</Link>
          <button style={S.addBtn} onClick={() => setShowAdd(true)}>+ ADD SOURCE</button>
        </div>
      </div>

      <div style={S.body}>
        <div style={S.panel}>
          <div style={S.label}>SOURCES</div>
          {slots.map((s, i) => (
            <div key={s.id} style={S.item(s.id === selectedId)} onClick={() => { setSelectedId(s.id); setInfo(null); }}>
              {i + 1}. {s.name}
            </div>
          ))}
          {slots.length === 0 && <div style={S.fv}>No video sources.<br />Click + ADD SOURCE to begin.</div>}
        </div>

        <div style={{ ...S.panel, ...S.center }}>
          <div style={S.label}>
            {selected ? `${HEADER_LABEL[selected.type]} ${info ? info.header : ""}` : "No source selected"}
          </div>
          {selected && View ? (
            <View key={selected.id} slot={selected} onInfo={setInfo} onRemove={() => remove(selected.id)} />
          ) : (
            <div style={S.box}><div style={S.hint}>SELECT OR ADD A SOURCE</div></div>
          )}
        </div>

        <div style={S.panel}>
          <div style={S.label}>SOURCE INFO</div>
          {selected ? (
            <>
              <div style={S.field}><div style={S.fl}>Name</div><div style={{ ...S.fv, color: C.text }}>{selected.name}</div></div>
              <div style={S.field}><div style={S.fl}>Type</div><div style={S.fv}>{TYPE_LABEL[selected.type]}</div></div>
              <div style={S.field}><div style={S.fl}>State</div><div style={S.fv}>{info ? info.state : "-"}</div></div>
              {info && info.extra.map(([k, v]) => (
                <div key={k} style={S.field}><div style={S.fl}>{k}</div><div style={S.fv}>{v}</div></div>
              ))}
              <div style={S.field}>
                <div style={S.fl}>Connection</div>
                <div style={{ ...S.fv, color: info ? info.connColor : C.dim }}>{info ? info.connection : "-"}</div>
              </div>
            </>
          ) : (
            <div style={S.fv}>Select a source to see details</div>
          )}

          {selected && info && info.log && info.log.length > 0 && (
            <>
              <div style={{ ...S.label, marginTop: 16 }}>DETECTION DASHBOARD</div>
              {info.log.slice(0, 8).map((e, i) => (
                <div key={i} style={{ ...S.fv, marginBottom: 6 }}>
                  <span style={{ color: C.text }}>{e.time}</span><br />
                  Persons {e.PERSON} | Vehicles {e.VEHICLE} | Faces {e.FACE}
                </div>
              ))}
            </>
          )}
        </div>
      </div>

      <div style={S.status}>Sources: {slots.length} | Local-first streaming workspace</div>

      {showAdd && <AddSourceDialog existing={slots.map((s) => s.name)} onCreate={create} onCancel={() => setShowAdd(false)} />}
    </div>
  );
}
