"""
IBVAP detection-service
Receives a single JPEG frame + camera_id, runs OpenCV-based detection, returns
bounding boxes for the Streaming Slots UI to draw, and (rate-limited) logs a
detection event to Supabase so the Dashboard/History UI can show it.

Two detectors are wired up out of the box because they need no extra model
download (they ship inside opencv-python):
  - Person detection: HOG + Linear SVM people detector
  - Face detection: Haar cascade

To add vehicle detection / ANPR / night-mode / virtual-fence later, add a new
function following the same pattern as `detect_people` / `detect_faces`, and
append its results into `all_detections` in the /detect endpoint.
"""

import os
import time
import io
import cv2
import numpy as np
import requests
from fastapi import FastAPI, UploadFile, Form
from fastapi.middleware.cors import CORSMiddleware

SUPABASE_URL = os.environ.get("SUPABASE_URL", "")
SUPABASE_KEY = os.environ.get("SUPABASE_KEY", "")
# Only log a given (camera, type) detection to the DB at most once per this many seconds,
# so a person standing in frame doesn't create thousands of rows.
LOG_COOLDOWN_SECONDS = 5

app = FastAPI(title="IBVAP Detection Service")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

hog = cv2.HOGDescriptor()
hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())

face_cascade = cv2.CascadeClassifier(
    cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
)

# last time we logged a detection of a given (camera_id, detection_type) to Supabase
_last_logged = {}


def detect_people(gray_small, scale_x, scale_y):
    rects, weights = hog.detectMultiScale(gray_small, winStride=(8, 8), padding=(8, 8), scale=1.05)
    results = []
    for (x, y, w, h), weight in zip(rects, weights):
        results.append({
            "type": "person",
            "confidence": float(weight),
            "box": {
                "x": int(x * scale_x), "y": int(y * scale_y),
                "w": int(w * scale_x), "h": int(h * scale_y),
            },
        })
    return results


def detect_faces(gray_small, scale_x, scale_y):
    faces = face_cascade.detectMultiScale(gray_small, scaleFactor=1.1, minNeighbors=5, minSize=(30, 30))
    results = []
    for (x, y, w, h) in faces:
        results.append({
            "type": "face",
            "confidence": 1.0,
            "box": {
                "x": int(x * scale_x), "y": int(y * scale_y),
                "w": int(w * scale_x), "h": int(h * scale_y),
            },
        })
    return results


def maybe_log_to_supabase(camera_id: str, detection: dict):
    if not SUPABASE_URL or not SUPABASE_KEY:
        return  # Supabase not configured yet — skip silently.

    key = (camera_id, detection["type"])
    now = time.time()
    if now - _last_logged.get(key, 0) < LOG_COOLDOWN_SECONDS:
        return
    _last_logged[key] = now

    try:
        requests.post(
            f"{SUPABASE_URL}/rest/v1/detections",
            headers={
                "apikey": SUPABASE_KEY,
                "Authorization": f"Bearer {SUPABASE_KEY}",
                "Content-Type": "application/json",
                "Prefer": "return=minimal",
            },
            json={
                "camera_id": camera_id,
                "detection_type": detection["type"],
                "confidence": detection["confidence"],
                "bounding_box": detection["box"],
            },
            timeout=3,
        )
    except requests.RequestException as e:
        print(f"[supabase log failed] {e}")


@app.post("/detect")
async def detect(camera_id: str = Form(...), frame: UploadFile = None):
    contents = await frame.read()
    np_arr = np.frombuffer(contents, np.uint8)
    img = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
    if img is None:
        return {"detections": [], "error": "could not decode image"}

    orig_h, orig_w = img.shape[:2]
    # Downscale for speed — detection runs much faster on a smaller frame.
    target_w = 480
    scale = target_w / orig_w if orig_w > target_w else 1.0
    small = cv2.resize(img, (int(orig_w * scale), int(orig_h * scale))) if scale != 1.0 else img
    gray_small = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY)
    scale_x, scale_y = orig_w / small.shape[1], orig_h / small.shape[0]

    all_detections = []
    all_detections += detect_people(gray_small, scale_x, scale_y)
    all_detections += detect_faces(gray_small, scale_x, scale_y)

    for d in all_detections:
        maybe_log_to_supabase(camera_id, d)

    return {"detections": all_detections}


@app.get("/health")
def health():
    return {"ok": True, "supabase_configured": bool(SUPABASE_URL and SUPABASE_KEY)}
