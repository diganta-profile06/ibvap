# IBVAP — Intelligent Border Video Analytics Platform
Team Coding Leyaks — SIH 2026 (Problem Statement 26187)

## Architecture (matches your whiteboard sketch)

```
[Phone browser] --(POST JPEG frames every ~400ms)--> [stream-server (Node)]
                                                            |
                                                            | forwards frame
                                                            v
                                                  [detection-service (Python + OpenCV)]
                                                            |
                                                            | logs detection events
                                                            v
                                                       [Supabase DB]
                                                        /          \
                                        [Dashboard UI (React)]  [History/Log UI (React)]
                                                            ^
                                        [Streaming Slots UI polls stream-server for
                                         latest frame + latest detection per camera]
```

Why this approach instead of raw WebRTC: full WebRTC needs a signaling exchange (SDP/ICE) and
usually a TURN server for NAT traversal, which is a lot to get reliable in a hackathon
timeframe. Posting frames on an interval ("motion-JPEG style") is much simpler to build,
debug, and demo, and is still "live" enough (a few hundred ms latency) for a demo. You can
swap this later for real WebRTC without changing the detection/DB/dashboard layers at all —
they only ever see frames/images, not the transport.

## Folders

- `frontend/` — React (Vite) app: Landing, Camera Capture, Streaming Slots, Dashboard, History
- `stream-server/` — Node/Express server. Receives frames from phones, holds "latest frame
  per camera" in memory, forwards frames to the detection service, exposes polling endpoints.
- `detection-service/` — Python FastAPI + OpenCV. Runs person detection on a frame, returns
  bounding boxes, and logs a row to Supabase when something is detected.
- `database/schema.sql` — Supabase/Postgres schema for cameras, detections, alerts.

## Running it locally (all on the same Wi-Fi as your phone)

1. **Database**: create a free Supabase project, open the SQL editor, run `database/schema.sql`.
   Copy your Project URL and anon public API key.

2. **detection-service**:
   ```bash
   cd detection-service
   pip install -r requirements.txt
   export SUPABASE_URL=https://xxxx.supabase.co
   export SUPABASE_KEY=your-anon-key
   uvicorn main:app --host 0.0.0.0 --port 8001
   ```

3. **stream-server**:
   ```bash
   cd stream-server
   npm install
   export DETECTION_SERVICE_URL=http://<your-laptop-ip>:8001
   node server.js
   # runs on port 4000
   ```

4. **frontend**:
   ```bash
   cd frontend
   npm install
   # edit src/config.js: set STREAM_SERVER_URL to http://<your-laptop-ip>:4000
   # edit src/supabaseClient.js with your Supabase URL + anon key
   npm run dev
   ```

5. On your **phone**, connect to the same Wi-Fi, open
   `http://<your-laptop-ip>:5173/camera/cam1` — grant camera permission, hit Start.

6. On your **laptop**, open `http://localhost:5173/` — click "Start IBVAP" →
   Streaming Slots should show the phone's feed within a second, with detection boxes
   once `detection-service` finds a person.

## Next steps once this skeleton runs

- Add more detectors in `detection-service/main.py` (vehicle, face/ANPR) — start with one
  working end-to-end before adding more.
- Style the Landing page with your actual team member names + demo video files.
- Replace the in-memory store in `stream-server` with Redis if you need multiple server
  instances (not needed for a hackathon demo).
