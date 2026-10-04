import { useEffect, useRef, useState } from "react";

// In-browser AI detection (people, vehicles, faces) using @vladmandic/human.
// The models are bundled in /models, so it works offline. This is a lighter
// model than the YOLO/SCRFD models inside the IBVAP desktop app, so results
// differ; the desktop app stays the reference detector.

let humanPromise = null;

function getHuman() {
  if (!humanPromise) {
    humanPromise = (async () => {
      const { Human } = await import("@vladmandic/human");
      const human = new Human({
        modelBasePath: "/models/",
        backend: "webgl",
        debug: false,
        filter: { enabled: false },
        face: {
          enabled: true,
          detector: { rotation: false, maxDetected: 10, minConfidence: 0.35 },
          mesh: { enabled: false },
          iris: { enabled: false },
          description: { enabled: false },
          emotion: { enabled: false },
          antispoof: { enabled: false },
          liveness: { enabled: false },
        },
        body: { enabled: false },
        hand: { enabled: false },
        gesture: { enabled: false },
        segmentation: { enabled: false },
        object: { enabled: true, maxDetected: 25, minConfidence: 0.25 },
      });
      await human.load();
      return human;
    })().catch((e) => {
      humanPromise = null;
      throw e;
    });
  }
  return humanPromise;
}

const VEHICLES = new Set(["car", "truck", "bus", "motorcycle", "bicycle", "train", "boat", "airplane"]);
const COLORS = { PERSON: "#3ec9d6", FACE: "#f2c14e", VEHICLE: "#ff6b5e" };

function classify(label) {
  if (label === "person") return "PERSON";
  if (VEHICLES.has(label)) return "VEHICLE";
  return null;
}

function drawBoxes(canvas, source, items) {
  const w = source.videoWidth || source.width;
  const h = source.videoHeight || source.height;
  if (!w || !h) return;
  if (canvas.width !== w) canvas.width = w;
  if (canvas.height !== h) canvas.height = h;
  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, w, h);
  ctx.lineWidth = Math.max(2, Math.round(w / 400));
  ctx.font = `${Math.max(12, Math.round(w / 55))}px 'Courier New', monospace`;
  ctx.textBaseline = "bottom";
  for (const it of items) {
    const [x, y, bw, bh] = it.box;
    ctx.strokeStyle = COLORS[it.type];
    ctx.strokeRect(x, y, bw, bh);
    ctx.fillStyle = COLORS[it.type];
    ctx.fillText(`${it.type} ${Math.round(it.score * 100)}%`, x + 2, y > 16 ? y - 2 : y + bh);
  }
}

// sourceRef: ref to a <video> or <canvas>; overlayRef: ref to the overlay canvas.
export function useDetector(sourceRef, overlayRef, enabled) {
  const [status, setStatus] = useState("off"); // off | loading | on | error
  const [counts, setCounts] = useState({ PERSON: 0, VEHICLE: 0, FACE: 0 });
  const [log, setLog] = useState([]); // newest first
  const lastLogAt = useRef(0);
  const lastCounts = useRef("");

  useEffect(() => {
    const clear = () => {
      const c = overlayRef.current;
      if (c) c.getContext("2d").clearRect(0, 0, c.width, c.height);
    };

    if (!enabled) {
      setStatus("off");
      clear();
      return undefined;
    }

    let stop = false;
    let timer = null;
    setStatus("loading");

    (async () => {
      const human = await getHuman();
      if (stop) return;
      setStatus("on");

      const loop = async () => {
        if (stop) return;
        const src = sourceRef.current;
        const ready =
          src &&
          ((src.tagName === "VIDEO" && src.readyState >= 2 && !src.paused && src.videoWidth) ||
            (src.tagName === "CANVAS" && src.width > 0 && src.height > 0));
        if (ready && overlayRef.current) {
          try {
            const res = await human.detect(src);
            if (stop) return;
            const items = [];
            for (const o of res.object || []) {
              const type = classify(o.label);
              if (type) items.push({ type, score: o.score, box: o.box });
            }
            for (const f of res.face || []) {
              if (f.box) items.push({ type: "FACE", score: f.boxScore || f.score || 0.5, box: f.box });
            }
            drawBoxes(overlayRef.current, src, items);

            const next = { PERSON: 0, VEHICLE: 0, FACE: 0 };
            items.forEach((i) => { next[i.type] += 1; });
            const key = `${next.PERSON}/${next.VEHICLE}/${next.FACE}`;
            if (key !== lastCounts.current) {
              lastCounts.current = key;
              setCounts(next);
            }

            const now = Date.now();
            if (items.length && now - lastLogAt.current > 2000) {
              lastLogAt.current = now;
              const time = new Date(now).toLocaleTimeString();
              setLog((l) => [{ time, ...next }, ...l].slice(0, 30));
            }
          } catch {
            /* skip this frame */
          }
        }
        timer = setTimeout(loop, 120);
      };
      loop();
    })().catch(() => { if (!stop) setStatus("error"); });

    return () => {
      stop = true;
      clearTimeout(timer);
      clear();
    };
  }, [enabled, sourceRef, overlayRef]);

  return { status, counts, log };
}
