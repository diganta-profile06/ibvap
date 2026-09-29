// Point this at your stream-server. When testing from your phone, this must be
// your laptop's LAN IP (e.g. http://192.168.1.5:4000), not "localhost" — the
// phone can't resolve "localhost" as your laptop.
export const STREAM_SERVER_URL =
  import.meta.env.VITE_STREAM_SERVER_URL || "http://localhost:4000";

// List the camera ids you want the desktop Streaming Slots page to display.
// A camera "exists" the moment a phone posts its first frame to
// /api/frames/:cameraId — you don't need to pre-register it here, but listing
// them gives the slots UI a fixed, predictable layout.
export const CAMERA_IDS = ["cam1", "cam2"];
