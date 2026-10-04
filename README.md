<div align="center" markdown="1">

# 🛡️ IBVAP — Intelligent Border Video Analytics Platform

**Real-time, multi-source video analytics for border surveillance, powered by C++, OpenCV and ncnn.**

Smart India Hackathon 2026 · Problem Statement **26187** · Team **Coding Leyaks**

[![SIH 2026](https://img.shields.io/badge/SIH-2026-orange)](#)
[![C++](https://img.shields.io/badge/Backend-C%2B%2B-00599C?logo=cplusplus)](#)
[![OpenCV](https://img.shields.io/badge/Vision-OpenCV%20%2B%20ncnn-5C3EE8?logo=opencv)](#)
[![SQLite](https://img.shields.io/badge/Database-SQLite-003B57?logo=sqlite)](#)
[![React](https://img.shields.io/badge/Frontend-React%20JS-61DAFB?logo=react)](#)

[⬇️ Download](https://drive.google.com/file/d/1TDX6YEvjIGLN1q29ceMaKtY9yeWHxFLX/view?usp=drive_link) · [📦 Repository](https://github.com/diganta-profile06/ibvap)

</div>

---

## 📌 Overview

**IBVAP** is an intelligent video analytics platform designed to assist border-security personnel by automatically detecting and logging events from multiple video sources in real time. Instead of operators watching feeds continuously, IBVAP processes each feed with an AI pipeline, highlights detections on screen, and stores every event in a searchable history log.

The platform is built for **flexibility in deployment**: it can ingest feeds from a mobile phone for instant testing, a laptop webcam for offline demos, pre-recorded footage for evaluation, or RTSP cameras for real installations.

---

## ✨ Key Features

- **Multiple input sources** through a unified *Video Source Manager*:
  - 📱 Mobile camera (instant testing, connected directly to the laptop over its hotspot)
  - 💻 Laptop camera (fully offline testing)
  - 🎞️ Pre-recorded video files
  - 📡 RTSP streams (IP / CCTV cameras)
- **AI detection engine** built on **OpenCV + ncnn** (C++) for fast, lightweight inference on CPU.
- **Live desktop User Interface** built with **Dear ImGui + OpenGL 3 + SDL 3**, showing feeds with detection overlays.
- **Dashboard UI** for at-a-glance status and analytics.
- **History Log UI** to review and search past detection events.
- **Local, zero-setup database** using **SQLite** — no cloud account required.
- **Works offline**, suitable for remote border locations with limited connectivity.

---

## 🏗️ System Architecture

```
 ┌─────────────────────┐
 │ Mobile Camera (.exe)│──┐
 └─────────────────────┘  │
 ┌─────────────────────┐  │      ┌──────────────────┐      ┌──────────────┐
 │ Laptop Camera       │──┼────▶ │  Video Source    │────▶ │  OpenCV AI   │
 └─────────────────────┘  │      │  Manager         │      │  (OpenCV+ncnn)│
 ┌─────────────────────┐  │      └──────────────────┘      └───────┬──────┘
 │ Pre-recorded Video  │──┤                                        │
 └─────────────────────┘  │                          ┌─────────────┼───────────────┐
 ┌─────────────────────┐  │                          ▼             ▼               ▼
 │ RTSP Stream         │──┘                    ┌──────────┐  ┌───────────┐  ┌────────────┐
 └─────────────────────┘                       │ Database │  │ Dashboard │  │ User       │
                                               │ (SQLite) │  │ UI        │  │ Interface  │
                                               └────┬─────┘  └───────────┘  └────────────┘
                                                    ▼
                                              ┌────────────┐
                                              │ History Log│
                                              │ UI         │
                                              └────────────┘
```

**Data flow**

1. Any supported source pushes frames to the **Video Source Manager**, which normalises them into a common frame format.
2. The **OpenCV AI** module runs detection on each frame and produces bounding boxes and labels.
3. Detection events are written to the **SQLite database**.
4. The **User Interface** displays live feeds with overlays, the **Dashboard UI** shows summaries, and the **History Log UI** reads past events from the database.

Because the detection, database and UI layers only consume *frames* and *events*, new sources (or a different transport such as WebRTC) can be added without changing the rest of the system.

---

## 🧰 Tech Stack

| Layer                    | Technology                          |
| ------------------------ | ----------------------------------- |
| User Interface           | Dear ImGui + OpenGL 3 + SDL 3       |
| Backend                  | C++                                 |
| Computer Vision & AI     | OpenCV + ncnn (C++)                 |
| Database                 | SQLite                              |
| Frontend (Web dashboard) | React JS                            |

---

## 📁 Project Structure

> Adjust folder names to match your repository if they differ.

```
ibvap/
├── backend/            # C++ core: Video Source Manager, detection pipeline, DB layer
│   ├── src/
│   ├── models/         # ncnn model files (.param / .bin)
│   └── CMakeLists.txt
├── ui/                 # Dear ImGui + OpenGL 3 + SDL 3 desktop interface
├── frontend/           # React JS dashboard & history log
├── database/           # SQLite schema and migrations
├── docs/               # Diagrams, screenshots, presentation
└── README.md
```

---

## 🚀 Quick Start (Windows Installer)

The fastest way to try IBVAP:

1. Click **[Download](https://drive.google.com/file/d/1TDX6YEvjIGLN1q29ceMaKtY9yeWHxFLX/view?usp=drive_link)** at the top of this page to open Google Drive, then click the download icon (⬇️). If Google warns it can't scan the file for viruses, click **Download anyway**.
2. Right-click the downloaded `.zip` file and choose **Extract All…**.
3. Open the extracted folder, run **`IBVAP_Setup.exe`** and follow the on-screen steps.
4. Launch **IBVAP** from the Start menu.
5. Choose a video source (mobile camera, laptop camera, video file or RTSP URL) and start analysing.

> ### ⚠️ Project Installation Note
> Our application is packaged using an optimized **Inno Setup** compiler. If **Microsoft Defender SmartScreen** displays a warning ("Windows protected your PC") upon launch, please click **"More info"** and then **"Run anyway"**.
> The warning appears because the installer is not code-signed, which is common for new open-source projects. The application is open-source, and its full source code can be reviewed in this repository.

---

## 🛠️ Build from Source

### Prerequisites

- C++17 compatible compiler (MSVC 2019+, GCC 9+ or Clang 10+)
- [CMake](https://cmake.org/) 3.16+
- [OpenCV](https://opencv.org/) 4.x
- [ncnn](https://github.com/Tencent/ncnn)
- [SDL 3](https://github.com/libsdl-org/SDL) and OpenGL 3.x capable GPU/driver
- [Dear ImGui](https://github.com/ocornut/imgui)
- [SQLite3](https://www.sqlite.org/)
- [Node.js](https://nodejs.org/) 18+ (for the React frontend)

### 1. Clone the repository

```bash
git clone https://github.com/diganta-profile06/ibvap.git
cd ibvap
```

### 2. Build the C++ backend and desktop UI

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### 3. Run the React frontend

```bash
cd frontend
npm install
npm run dev
```

The dashboard is served at `http://localhost:5173/`.

---

## 🎥 Supported Video Sources

| Source             | Use case                     | How to use                                              |
| ------------------ | ---------------------------- | ------------------------------------------------------- |
| Mobile camera      | Instant testing              | Install the `.exe`, turn on the laptop's Mobile Hotspot and connect the phone to it (direct peer-to-peer link) |
| Laptop camera      | Offline testing              | Select the built-in webcam in the source list           |
| Pre-recorded video | Evaluation & demos           | Choose a local video file (`.mp4`, `.avi`, etc.)        |
| RTSP               | Real CCTV / IP cameras       | Enter a URL such as `rtsp://<user>:<pass>@<ip>:554/stream` |

### 📱 Connecting a Mobile Camera (Peer-to-Peer)

The phone and laptop do **not** need to share an existing Wi-Fi router. IBVAP works over a direct, private peer-to-peer network created by the laptop itself, so it also works in remote areas with no internet.

1. On the laptop, open **Settings → Network & Internet → Mobile hotspot** and turn it **On**. Note the network name and password.
2. On the phone, open Wi-Fi settings and **connect to the laptop's hotspot**.
3. Start IBVAP on the laptop and select **Mobile Camera** as the video source.
4. Open the connection address shown by IBVAP on the phone and allow camera access. The live feed appears in IBVAP.

> The phone will have no internet while connected to the laptop's hotspot. This is expected, because all video stays on this private local network.

---

## 🗄️ Database

IBVAP stores detection events locally in **SQLite**, so no external service or account is needed. A typical event record contains:

- Event ID and timestamp
- Source / camera identifier
- Detected object class and confidence
- Bounding-box coordinates
- Optional snapshot reference

The History Log UI and Dashboard UI query this database to display past events and summary statistics.

---

## 🎬 Demo Videos

| Demo | Description | Watch |
| ---- | ----------- | :---: |
| 🌡️ **Thermal Detection** | Detection on thermal imagery for low-visibility and night conditions | [▶ Watch](https://drive.google.com/file/d/1ZnBVc7YIK7K_A7B3Q3OWrDvuamqsMsZR/view?usp=drivesdk) |
| 🙂 **Face Recognition** | Identifying faces in live and recorded feeds | [▶ Watch](https://drive.google.com/file/d/1GHHAvQMsN6qovOwz2C2bA_sVjWvBc6cL/view?usp=drivesdk) |
| 🚧 **Virtual Fence** | Detecting intrusions when a person or object crosses a defined boundary | [▶ Watch](https://drive.google.com/file/d/1aKR4i98EcWHFGDycz9wFBtqOIyVHYy4a/view?usp=drivesdk) |
| 🚗 **Vehicle Detection** | Detecting and tracking vehicles in the monitored area | [▶ Watch](https://drive.google.com/file/d/1ro6woOqg_Xr2_u6MWc0cgZsf47cshHjm/view?usp=drivesdk) |

---

## 🗺️ Roadmap

- [ ] Additional detectors (e.g. ANPR / number-plate recognition)
- [ ] Alert rules and notifications for restricted zones
- [ ] Multi-camera synchronised view
- [ ] Optional migration to WebRTC transport for mobile streaming
- [ ] Exportable reports (PDF / CSV) from the History Log

---

## 👥 Team — Coding Leyaks

| Name | Role | Focus Area |
| ---- | ---- | ---------- |
| **Diganta Pal** | Team Lead | Project coordination, architecture, system integration |
| **Abhranil Mallick** | Computer Vision & AI | OpenCV + ncnn detection pipeline, model optimisation |
| **Biprajit Biswas** | Backend | C++ core, Video Source Manager (mobile, laptop, video file, RTSP) |
| **Sagnik Das** | Database | SQLite schema, event logging, History Log data layer |
| **Utsav Das** | QA, Documentation & Presentation | Testing, README, demo video, SIH presentation |
| **Saptaparna Das** | UI / Frontend | Dear ImGui + OpenGL 3 + SDL 3 interface, React JS Dashboard & History Log |

---

## 📄 License

This project was developed for **Smart India Hackathon 2026**. Add your preferred license here (e.g. MIT) by including a `LICENSE` file in the repository.

---

<div align="center" markdown="1">

**Built with ❤️ by Team Coding Leyaks for SIH 2026**

</div>
