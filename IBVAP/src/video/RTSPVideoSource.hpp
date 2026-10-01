#pragma once

#include "VideoSource.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace cv
{
class VideoCapture;
}

class RTSPVideoSource final
    : public VideoSource
{
public:
    RTSPVideoSource(
        const std::string& url,
        const std::string& displayName
    );

    ~RTSPVideoSource() override;

    RTSPVideoSource(
        const RTSPVideoSource&
    ) = delete;

    RTSPVideoSource& operator=(
        const RTSPVideoSource&
    ) = delete;

    bool start() override;

    void stop() noexcept override;

    VideoSourceState state()
        const noexcept override;

    bool tryGetFrame(
        Frame& frame
    ) override;

    const VideoSourceInfo&
    info() const noexcept override;

private:
    void captureLoop();

    void setState(
        VideoSourceState newState
    );

private:
    std::string m_url;

    VideoSourceInfo m_info;

    mutable std::mutex m_stateMutex;

    VideoSourceState m_state =
        VideoSourceState::Created;

    std::atomic<bool> m_stopRequested =
        false;

    std::thread m_worker;

    mutable std::mutex m_frameMutex;

    Frame m_latestFrame;

    std::uint64_t m_sequence = 0;
};