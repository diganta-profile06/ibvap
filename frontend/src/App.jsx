import { Routes, Route, Navigate } from "react-router-dom";
import MainPage from "./pages/MainPage.jsx";
import Instructions from "./pages/Instructions.jsx";
import DemoVideos from "./pages/DemoVideos.jsx";
import { useParams } from "react-router-dom";
import { IBVAP_URL } from "./config.js";
import StreamingSlots from "./pages/StreamingSlots.jsx";
import Dashboard from "./pages/Dashboard.jsx";
import History from "./pages/History.jsx";

// The phone camera page is served by the IBVAP desktop app itself.
function CameraRedirect() {
  const { cameraId } = useParams();
  window.location.replace(`${IBVAP_URL}/mobile?source=${encodeURIComponent(cameraId)}`);
  return null;
}

export default function App() {
  return (
    <Routes>
      <Route path="/" element={<MainPage />} />
      <Route path="/instructions" element={<Instructions />} />
      <Route path="/demo-videos" element={<DemoVideos />} />
      <Route path="/ibvap-dashboard" element={<StreamingSlots />} />
      <Route path="/streaming" element={<Navigate to="/ibvap-dashboard" replace />} />
      <Route path="/camera/:cameraId" element={<CameraRedirect />} />
      <Route path="/dashboard" element={<Dashboard />} />
      <Route path="/history" element={<History />} />
    </Routes>
  );
}
