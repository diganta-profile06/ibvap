import { Routes, Route } from "react-router-dom";
import MainPage from "./pages/MainPage.jsx";
import Instructions from "./pages/Instructions.jsx";
import DemoVideos from "./pages/DemoVideos.jsx";
import CameraCapture from "./pages/CameraCapture.jsx";
import StreamingSlots from "./pages/StreamingSlots.jsx";
import Dashboard from "./pages/Dashboard.jsx";
import History from "./pages/History.jsx";

export default function App() {
  return (
    <Routes>
      <Route path="/" element={<MainPage />} />
      <Route path="/instructions" element={<Instructions />} />
      <Route path="/demo-videos" element={<DemoVideos />} />
      <Route path="/ibvap-dashboard" element={<StreamingSlots />} />
      <Route path="/camera/:cameraId" element={<CameraCapture />} />
      <Route path="/dashboard" element={<Dashboard />} />
      <Route path="/history" element={<History />} />
    </Routes>
  );
}
