// Where the IBVAP desktop app is running. Its built-in HTTPS server (port 8443)
// already relays each phone's H.264 stream over a WebSocket, so this web app
// just connects to it. Nothing in IBVAP has to be changed.
//
// - Default: the same PC you opened this page on (https://localhost:8443).
// - Opening this page from another device? Set VITE_IBVAP_URL, e.g.
//   VITE_IBVAP_URL=https://192.168.0.108:8443
//
// Note: the first time, open this URL once in the browser and accept the
// self-signed certificate warning, otherwise the live video cannot connect.
export const IBVAP_URL =
  import.meta.env.VITE_IBVAP_URL || `https://${window.location.hostname}:8443`;

export const IBVAP_WS_URL = IBVAP_URL.replace(/^http/, "ws") + "/mobile/viewer/ws";
