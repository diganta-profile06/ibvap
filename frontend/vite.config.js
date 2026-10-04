import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));

// In dev, forward the API and phone page to the running IBVAP desktop app.
// IBVAP uses a self-signed certificate, hence secure: false.
const IBVAP = process.env.IBVAP_TARGET || "https://localhost:8443";

/* ------------------------------------------------------------------ *
 * START IBVAP -> opens the installed desktop app directly.
 * A browser page cannot start a program, but this dev/preview server runs
 * on the same PC, so it can: POST /__ibvap/launch finds IBVAP.exe and starts
 * it. No registry change, no browser prompt. Only the PC itself may call it.
 * ------------------------------------------------------------------ */

function findIbvapExe() {
  if (process.env.IBVAP_EXE && fs.existsSync(process.env.IBVAP_EXE)) return process.env.IBVAP_EXE;

  const saved = path.join(here, "ibvap-path.txt");
  if (fs.existsSync(saved)) {
    const p = fs.readFileSync(saved, "utf8").trim();
    if (p && fs.existsSync(p)) return p;
  }

  const script = path.join(here, "..", "installer", "register_ibvap_protocol.ps1");
  if (fs.existsSync(script)) {
    const r = spawnSync(
      "powershell",
      ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-FindOnly"],
      { encoding: "utf8", windowsHide: true }
    );
    const p = (r.stdout || "").trim().split(/\r?\n/).pop();
    if (r.status === 0 && p && fs.existsSync(p)) {
      try { fs.writeFileSync(saved, p); } catch { /* ignore */ }
      return p;
    }
  }
  return null;
}

function isRunning() {
  const r = spawnSync("tasklist", ["/FI", "IMAGENAME eq IBVAP.exe", "/NH"], { encoding: "utf8", windowsHide: true });
  return /IBVAP\.exe/i.test(r.stdout || "");
}

function bringToFront() {
  spawnSync(
    "powershell",
    ["-NoProfile", "-Command", "(New-Object -ComObject WScript.Shell).AppActivate('IBVAP') | Out-Null"],
    { windowsHide: true }
  );
}

function ibvapLauncher() {
  const handler = (req, res, next) => {
    if (!req.url || !req.url.startsWith("/__ibvap/launch")) return next();

    const send = (code, body) => {
      res.statusCode = code;
      res.setHeader("Content-Type", "application/json");
      res.end(JSON.stringify(body));
    };

    const addr = req.socket.remoteAddress || "";
    const local = addr === "127.0.0.1" || addr === "::1" || addr === "::ffff:127.0.0.1";
    if (!local) return send(403, { status: "forbidden" });
    if (req.method !== "POST") return send(405, { status: "use-post" });
    if (process.platform !== "win32") return send(501, { status: "windows-only" });

    const exe = findIbvapExe();
    if (!exe) return send(404, { status: "not-found" });

    if (isRunning()) {
      bringToFront();
      return send(200, { status: "already-running", exe });
    }

    try {
      const child = spawn(exe, [], { cwd: path.dirname(exe), detached: true, stdio: "ignore" });
      child.on("error", () => {});
      child.unref();
      return send(200, { status: "started", exe });
    } catch (e) {
      return send(500, { status: "failed", error: String(e) });
    }
  };

  return {
    name: "ibvap-launcher",
    configureServer(server) { server.middlewares.use(handler); },
    configurePreviewServer(server) { server.middlewares.use(handler); },
  };
}

export default defineConfig({
  plugins: [react(), ibvapLauncher()],
  server: {
    host: true,
    port: 5173,
    proxy: {
      "/api": { target: IBVAP, secure: false, changeOrigin: true },
      "/mobile": { target: IBVAP, secure: false, changeOrigin: true, ws: true },
    },
  },
  preview: { port: 5173 },
});
