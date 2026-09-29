import { useEffect, useRef, useState } from "react";
import { useParams } from "react-router-dom";
import { STREAM_SERVER_URL } from "../config.js";

const FRAME_INTERVAL_MS = 400; // ~2.5 fps upload rate — plenty for a demo, keeps bandwidth low

export default function CameraCapture() {
  const { cameraId } = useParams();
  const videoRef = useRef(null);
  const canvasRef = useRef(document.createElement("canvas"));
  const streamRef = useRef(null);
  const intervalRef = useRef(null);

  const [isStreaming, setIsStreaming] = useState(false);
  const [facingMode, setFacingMode] = useState("environment"); // back camera by default
  const [status, setStatus] = useState("Idle");
  const [framesSent, setFramesSent] = useState(0);
  const [error, setError] = useState(null);

  async function startCamera() {
    setError(null);
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        video: { facingMode },
        audio: false,
      });
      streamRef.current = stream;
      videoRef.current.srcObject = stream;
      await videoRef.current.play();
      setIsStreaming(true);
      setStatus("Live — sending frames");
      intervalRef.current = setInterval(sendFrame, FRAME_INTERVAL_MS);
    } catch (err) {
      setError(err.message || "Could not access camera");
    }
  }

  function stopCamera() {
    if (intervalRef.current) clearInterval(intervalRef.current);
    if (streamRef.current) {
      streamRef.current.getTracks().forEach((t) => t.stop());
      streamRef.current = null;
    }
    setIsStreaming(false);
    setStatus("Stopped");
  }

  async function switchCamera() {
    const next = facingMode === "environment" ? "user" : "environment";
    setFacingMode(next);
    if (isStreaming) {
      stopCamera();
      // slight delay so the browser releases the previous camera first
      setTimeout(() => startCamera(), 300);
    }
  }

  function sendFrame() {
    const video = videoRef.current;
    const canvas = canvasRef.current;
    if (!video || video.readyState < 2) return;

    canvas.width = video.videoWidth;
    canvas.height = video.videoHeight;
    const ctx = canvas.getContext("2d");
    ctx.drawImage(video, 0, 0, canvas.width, canvas.height);

    canvas.toBlob(
      async (blob) => {
        if (!blob) return;
        const form = new FormData();
        form.append("frame", blob, "frame.jpg");
        try {
          const resp = await fetch(`${STREAM_SERVER_URL}/api/frames/${cameraId}`, {
            method: "POST",
            body: form,
          });
          if (resp.ok) setFramesSent((n) => n + 1);
        } catch (err) {
          setStatus("Upload failed — check STREAM_SERVER_URL / network");
        }
      },
      "image/jpeg",
      0.7
    );
  }

  useEffect(() => {
    return () => stopCamera(); // cleanup on unmount
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <div className="page" style={{ maxWidth: 480, margin: "0 auto", textAlign: "center" }}>
      <h2 style={{ marginBottom: 4 }}>Camera: {cameraId}</h2>
      <div style={{ color: "var(--text-dim)", marginBottom: 16, fontSize: 14 }}>{status}</div>

      <video
        ref={videoRef}
        playsInline
        muted
        style={{ width: "100%", borderRadius: 12, background: "#000", aspectRatio: "3/4" }}
      />

      {error && (
        <div style={{ color: "var(--danger)", marginTop: 12, fontSize: 14 }}>{error}</div>
      )}

      <div style={{ display: "flex", gap: 12, justifyContent: "center", marginTop: 20 }}>
        {!isStreaming ? (
          <button className="btn" onClick={startCamera}>Start</button>
        ) : (
          <button className="btn danger" onClick={stopCamera}>Stop</button>
        )}
        <button className="btn secondary" onClick={switchCamera}>Switch camera</button>
      </div>

      <div style={{ marginTop: 20, color: "var(--text-dim)", fontSize: 13 }}>
        Frames sent: {framesSent}
      </div>
      <div style={{ marginTop: 4, color: "var(--text-dim)", fontSize: 12 }}>
        Sending to {STREAM_SERVER_URL}
      </div>
    </div>
  );
}
