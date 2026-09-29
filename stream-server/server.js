// IBVAP stream-server
// Receives JPEG frames posted by phone cameras, keeps the latest frame + latest
// detection result per camera in memory, and forwards frames to the Python
// detection-service asynchronously so the desktop UI can poll for both.

import express from "express";
import cors from "cors";
import multer from "multer";
import fetch from "node-fetch";
import FormData from "form-data";

const PORT = process.env.PORT || 4000;
const DETECTION_SERVICE_URL =
  process.env.DETECTION_SERVICE_URL || "http://localhost:8001";

const app = express();
app.use(cors());
const upload = multer({ storage: multer.memoryStorage(), limits: { fileSize: 5 * 1024 * 1024 } });

// In-memory store: { [cameraId]: { frame: Buffer, mimeType, updatedAt, detections } }
const cameraStore = new Map();

function getCamera(cameraId) {
  if (!cameraStore.has(cameraId)) {
    cameraStore.set(cameraId, { frame: null, mimeType: "image/jpeg", updatedAt: null, detections: [] });
  }
  return cameraStore.get(cameraId);
}

// Phone posts a frame here.
app.post("/api/frames/:cameraId", upload.single("frame"), async (req, res) => {
  const { cameraId } = req.params;
  if (!req.file) return res.status(400).json({ error: "no frame uploaded" });

  const cam = getCamera(cameraId);
  cam.frame = req.file.buffer;
  cam.mimeType = req.file.mimetype || "image/jpeg";
  cam.updatedAt = Date.now();
  res.json({ ok: true });

  // Fire-and-forget: send this frame to the detection service, store the result
  // for the next poll. We don't await this before responding to the phone, so
  // frame uploads never wait on detection latency.
  try {
    const form = new FormData();
    form.append("camera_id", cameraId);
    form.append("frame", cam.frame, { filename: "frame.jpg", contentType: cam.mimeType });

    const resp = await fetch(`${DETECTION_SERVICE_URL}/detect`, {
      method: "POST",
      body: form,
    });
    if (resp.ok) {
      const data = await resp.json();
      cam.detections = data.detections || [];
    }
  } catch (err) {
    // Detection service being down shouldn't break streaming.
    console.error(`[detect] failed for ${cameraId}:`, err.message);
  }
});

// Desktop polls this for the live image.
app.get("/api/frames/:cameraId/latest", (req, res) => {
  const cam = cameraStore.get(req.params.cameraId);
  if (!cam || !cam.frame) return res.status(404).json({ error: "no frame yet" });
  res.set("Content-Type", cam.mimeType);
  res.set("Cache-Control", "no-store");
  res.send(cam.frame);
});

// Desktop polls this for the latest bounding boxes to overlay on the image.
app.get("/api/frames/:cameraId/detections", (req, res) => {
  const cam = cameraStore.get(req.params.cameraId);
  if (!cam) return res.json({ detections: [], updatedAt: null });
  res.json({ detections: cam.detections, updatedAt: cam.updatedAt });
});

// Lists all camera ids that have sent at least one frame, and whether they're "live"
// (frame received in the last 5 seconds).
app.get("/api/cameras", (req, res) => {
  const now = Date.now();
  const cameras = Array.from(cameraStore.entries()).map(([id, cam]) => ({
    id,
    isLive: cam.updatedAt ? now - cam.updatedAt < 5000 : false,
    updatedAt: cam.updatedAt,
  }));
  res.json({ cameras });
});

app.get("/health", (req, res) => res.json({ ok: true }));

app.listen(PORT, () => {
  console.log(`IBVAP stream-server listening on http://0.0.0.0:${PORT}`);
  console.log(`Forwarding frames to detection-service at ${DETECTION_SERVICE_URL}`);
});
