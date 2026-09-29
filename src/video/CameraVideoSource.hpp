#pragma once

#include "VideoSource.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace cv
{
    class VideoCapture;
}

class CameraVideoSource final : public VideoSource
{
public:
    explicit CameraVideoSource(
        int cameraIndex,
        std::string displayName
    );

    ~CameraVideoSource() override;

    CameraVideoSource(
        const CameraVideoSource&
    ) = delete;

    CameraVideoSource& operator=(
        const CameraVideoSource&
    ) = delete;

    bool start() override;

    void stop() noexcept override;

    VideoSourceState state()
        const noexcept override;

    bool tryGetFrame(
        Frame& frame
    ) override;

    const VideoSourceInfo& info()
        const noexcept override;

private:
    void captureLoop();

    bool openCamera();

    void closeCamera();

    void pushFrame(
        Frame&& frame
    );

private:
    VideoSourceInfo m_info;

    int m_cameraIndex = 0;

    std::unique_ptr<cv::VideoCapture> m_capture;

    std::thread m_captureThread;

    std::atomic<VideoSourceState> m_state{
        VideoSourceState::Created
    };

    std::atomic<bool> m_stopRequested{
        false
    };

    /*
     * Keep the camera queue deliberately small.
     *
     * For live surveillance, old frames are less
     * useful than the newest frame. This prevents
     * latency from continuously increasing if
     * rendering or AI temporarily falls behind.
     */
    static constexpr std::size_t
        MaxQueuedFrames = 3;

    std::deque<Frame> m_frameQueue;

    mutable std::mutex m_queueMutex;

    std::condition_variable
        m_queueCondition;
};