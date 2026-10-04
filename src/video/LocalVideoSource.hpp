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

class LocalVideoSource final : public VideoSource
{
public:
    explicit LocalVideoSource(
        std::string filePath,
        std::string displayName
    );

    ~LocalVideoSource() override;

    LocalVideoSource(
        const LocalVideoSource&
    ) = delete;

    LocalVideoSource& operator=(
        const LocalVideoSource&
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

    // ---------------------------------------------------------
    // Local-video-only operation
    // ---------------------------------------------------------

    bool restart();

private:
    void decodeLoop();

    bool openVideo();

    void closeVideo();

    void pushFrame(
        Frame&& frame
    );

private:
    VideoSourceInfo m_info;

    std::string m_filePath;

    std::unique_ptr<cv::VideoCapture> m_capture;

    std::thread m_decodeThread;

    std::atomic<VideoSourceState> m_state{
        VideoSourceState::Created
    };

    std::atomic<bool> m_stopRequested{
        false
    };

    static constexpr std::size_t
        MaxQueuedFrames = 3;

    std::deque<Frame> m_frameQueue;

    mutable std::mutex m_queueMutex;

    std::condition_variable
        m_queueCondition;
};