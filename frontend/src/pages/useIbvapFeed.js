import { useEffect, useRef, useState } from "react";
import { IBVAP_WS_URL } from "../config.js";

// Receives the phone's H.264 (Annex-B) access units from the IBVAP desktop app
// (/mobile/viewer/ws) and decodes them in the browser with WebCodecs, drawing
// each frame on the given canvas. No change to IBVAP is needed.

// Returns the NAL unit types found in an Annex-B buffer, with the offset of
// each NAL's first byte.
export function findNals(bytes) {
  const nals = [];
  const n = bytes.length;
  for (let i = 0; i + 3 < n; i++) {
    if (bytes[i] === 0 && bytes[i + 1] === 0) {
      let start = -1;
      if (bytes[i + 2] === 1) start = i + 3;
      else if (bytes[i + 2] === 0 && bytes[i + 3] === 1) start = i + 4;
      if (start > 0 && start < n) {
        nals.push({ type: bytes[start] & 0x1f, offset: start });
        i = start;
      }
    }
  }
  return nals;
}

function hex2(v) {
  return v.toString(16).padStart(2, "0");
}

export function useIbvapFeed(sourceId, canvasRef) {
  const [status, setStatus] = useState("idle"); // idle | connecting | waiting | live | error
  const [size, setSize] = useState(null);

  useEffect(() => {
    if (!sourceId) {
      setStatus("idle");
      return undefined;
    }
    if (typeof VideoDecoder === "undefined") {
      setStatus("error");
      return undefined;
    }

    let closed = false;
    let ws = null;
    let decoder = null;
    let configured = false;
    let gotKeyframe = false;
    let lastFrameAt = 0;
    let retryTimer = null;
    let registerTimer = null;
    let watchdog = null;

    function resetDecoder() {
      try { if (decoder && decoder.state !== "closed") decoder.close(); } catch { /* ignore */ }
      decoder = null;
      configured = false;
      gotKeyframe = false;
    }

    function ensureDecoder(bytes, nals) {
      if (decoder && configured) return true;
      const sps = nals.find((x) => x.type === 7);
      if (!sps || sps.offset + 3 >= bytes.length) return false;
      const codec = `avc1.${hex2(bytes[sps.offset + 1])}${hex2(bytes[sps.offset + 2])}${hex2(bytes[sps.offset + 3])}`;

      decoder = new VideoDecoder({
        output: (frame) => {
          const canvas = canvasRef.current;
          if (canvas) {
            if (canvas.width !== frame.displayWidth || canvas.height !== frame.displayHeight) {
              canvas.width = frame.displayWidth;
              canvas.height = frame.displayHeight;
              setSize({ w: frame.displayWidth, h: frame.displayHeight });
            }
            canvas.getContext("2d").drawImage(frame, 0, 0);
          }
          frame.close();
          lastFrameAt = Date.now();
          setStatus("live");
        },
        error: () => {
          resetDecoder(); // wait for the next keyframe and start again
        },
      });
      decoder.configure({
        codec,
        avc: { format: "annexb" },
        optimizeForLatency: true,
      });
      configured = true;
      return true;
    }

    function onBinary(buffer) {
      const bytes = new Uint8Array(buffer);
      const nals = findNals(bytes);
      const isKey = nals.some((x) => x.type === 5);

      if (!gotKeyframe) {
        if (!isKey) return; // can't start on a delta frame
        if (!ensureDecoder(bytes, nals)) return;
        gotKeyframe = true;
      }
      if (!decoder || decoder.state !== "configured") return;
      if (decoder.decodeQueueSize > 4) return; // drop if we're behind

      try {
        decoder.decode(
          new EncodedVideoChunk({
            type: isKey ? "key" : "delta",
            timestamp: performance.now() * 1000,
            data: bytes,
          })
        );
      } catch {
        resetDecoder();
      }
    }

    function connect() {
      if (closed) return;
      setStatus("connecting");
      ws = new WebSocket(IBVAP_WS_URL);
      ws.binaryType = "arraybuffer";

      ws.onopen = () => {
        setStatus("waiting");
        // IBVAP ignores VIEWER: until the phone has registered this source,
        // so keep asking until video starts to arrive.
        const register = () => {
          if (ws && ws.readyState === WebSocket.OPEN) ws.send(`VIEWER:${sourceId}`);
        };
        register();
        registerTimer = setInterval(() => {
          if (Date.now() - lastFrameAt > 2000) register();
        }, 1000);
      };

      ws.onmessage = (ev) => {
        if (ev.data instanceof ArrayBuffer) onBinary(ev.data);
      };

      ws.onclose = () => {
        clearInterval(registerTimer);
        resetDecoder();
        if (!closed) {
          setStatus((s) => (s === "live" ? "waiting" : "error"));
          retryTimer = setTimeout(connect, 2000);
        }
      };

      ws.onerror = () => {
        try { ws.close(); } catch { /* ignore */ }
      };
    }

    // If frames stop arriving (phone left), fall back to "waiting".
    watchdog = setInterval(() => {
      if (lastFrameAt && Date.now() - lastFrameAt > 3000) {
        lastFrameAt = 0;
        resetDecoder();
        setStatus("waiting");
      }
    }, 1000);

    connect();

    return () => {
      closed = true;
      clearTimeout(retryTimer);
      clearInterval(registerTimer);
      clearInterval(watchdog);
      resetDecoder();
      try { if (ws) ws.close(); } catch { /* ignore */ }
    };
  }, [sourceId, canvasRef]);

  return { status, size };
}
