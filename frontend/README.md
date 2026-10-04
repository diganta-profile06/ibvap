# IBVAP web frontend (works with the unmodified IBVAP desktop app)

## Run the web pages
    cd frontend
    npm install
    npm run dev          # http://localhost:5173

## START IBVAP opens the desktop app
The buttons open the installed IBVAP.exe through an `ibvap://` link.
Register it ONCE per Windows user: double-click `installer\register_ibvap_protocol.bat`.
(The ready-made IBVAP_Setup.exe does NOT do this by itself; an installer built from
`installer\IBVAP.iss` does.) The script finds the installed IBVAP.exe automatically,
registers the link, and offers to test it.
Then click START IBVAP; Chrome/Edge ask "Open IBVAP?" -> Open.

If nothing happens:
- Win+R, type `ibvap://launch`, Enter. If IBVAP does not start, the link is not registered
  (run the .bat, and pick IBVAP.exe when asked).
- Check in PowerShell: `reg query HKCU\Software\Classes\ibvap\shell\open\command`
- Chrome only shows the prompt after a real click and only for registered links.

## START IBVAP opens the app directly (recommended)
Double-click `Start_IBVAP_Web.bat` (project root). It starts the web page and opens
http://localhost:5173. Click **START IBVAP** and the installed IBVAP.exe starts by itself
(no browser prompt, nothing to register). If IBVAP is already running it is brought to
the front. The path is found automatically; to force one, put the full path of IBVAP.exe
in `frontend\ibvap-path.txt`. Only this PC can trigger the launch.

## Web workspace (/ibvap-dashboard)
+ ADD SOURCE opens the same "Add Video Source" window as the desktop app:
- Mobile Camera: type the name the phone uses (`https://<pc-ip>:8443/mobile?source=NAME`).
  Needs IBVAP running; open https://localhost:8443 once and accept the certificate.
- Pre-recorded video: SELECT VIDEO FROM DEVICE, REPLAY, FULL SCREEN, REMOVE.
- Laptop Camera: uses this PC's camera through the browser.
- RTSP Stream: the URL is saved; browsers cannot play RTSP (use the desktop app).
AI detection boxes, the detection dashboard and the virtual fence stay in the desktop app.

## AI detection in the web workspace
Each video slot has **AI DETECTION: ON/OFF**. It runs a light in-browser model
(people, vehicles, faces; bundled in `public/models`, works offline) and draws boxes on
the video, with counts and a small detection log on the right. It is weaker than the
YOLO/SCRFD models in the IBVAP desktop app (no weapons, no plate reading), which stays the
reference detector. The first start loads the model for a few seconds.
