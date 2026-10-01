#include "LocalVideoSource.hpp"

#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

LocalVideoSource::LocalVideoSource(
    std::string filePath,
    std::string displayName
)
    : m_filePath(std::move(filePath))
{
    m_info.id =
        m_filePath;

    m_info.name =
        std::move(displayName);

    m_info.type =
        VideoSourceType::LocalVideo;
}

LocalVideoSource::~LocalVideoSource()
{
    stop();
}

bool LocalVideoSource::start()
{
    VideoSourceState expected =
        VideoSourceState::Created;

    if (!m_state.compare_exchange_strong(
            expected,
            VideoSourceState::Starting))
    {
        return false;
    }

    // A previous decoder thread must never
    // remain joinable when starting again.
    if (m_decodeThread.joinable())
    {
        m_decodeThread.join();
    }

    m_stopRequested.store(
        false,
        std::memory_order_release
    );

    if (!openVideo())
    {
        m_state.store(
            VideoSourceState::Error,
            std::memory_order_release
        );

        return false;
    }

    m_decodeThread =
        std::thread(
            &LocalVideoSource::decodeLoop,
            this
        );

    return true;
}

bool LocalVideoSource::restart()
{
    stop();

    closeVideo();

    {
        std::lock_guard<std::mutex> lock(
            m_queueMutex
        );

        m_frameQueue.clear();
    }

    m_stopRequested.store(
        false,
        std::memory_order_release
    );

    m_state.store(
        VideoSourceState::Created,
        std::memory_order_release
    );

    return start();
}

void LocalVideoSource::stop() noexcept
{
    m_stopRequested.store(
        true,
        std::memory_order_release
    );

    m_queueCondition.notify_all();

    // Always join a joinable thread.
    //
    // This is important when a video naturally
    // reaches its end. The source can already
    // report Stopped while the std::thread is
    // still joinable.
    if (m_decodeThread.joinable())
    {
        m_decodeThread.join();
    }

    closeVideo();

    {
        std::lock_guard<std::mutex> lock(
            m_queueMutex
        );

        m_frameQueue.clear();
    }

    m_state.store(
        VideoSourceState::Stopped,
        std::memory_order_release
    );
}

VideoSourceState
LocalVideoSource::state()
    const noexcept
{
    return m_state.load(
        std::memory_order_acquire
    );
}

const VideoSourceInfo&
LocalVideoSource::info()
    const noexcept
{
    return m_info;
}

bool LocalVideoSource::tryGetFrame(
    Frame& frame
)
{
    std::lock_guard<std::mutex> lock(
        m_queueMutex
    );

    if (m_frameQueue.empty())
    {
        return false;
    }

    // We only need the newest available frame.
    //
    // This prevents latency from accumulating
    // when rendering cannot consume frames fast
    // enough.
    frame =
        std::move(
            m_frameQueue.back()
        );

    m_frameQueue.clear();

    return frame.valid();
}

bool LocalVideoSource::openVideo()
{
    m_capture =
        std::make_unique<cv::VideoCapture>(
            m_filePath
        );

    if (!m_capture->isOpened())
    {
        std::cerr
            << "LocalVideoSource: "
               "Failed to open video:\n"
            << "  "
            << m_filePath
            << '\n';

        m_capture.reset();

        return false;
    }

    const double width =
        m_capture->get(
            cv::CAP_PROP_FRAME_WIDTH
        );

    const double height =
        m_capture->get(
            cv::CAP_PROP_FRAME_HEIGHT
        );

    const double fps =
        m_capture->get(
            cv::CAP_PROP_FPS
        );

    if (width <= 0.0 ||
        height <= 0.0)
    {
        std::cerr
            << "LocalVideoSource: "
               "Invalid video dimensions.\n";

        m_capture->release();

        m_capture.reset();

        return false;
    }

    m_info.width =
        static_cast<std::uint32_t>(
            width
        );

    m_info.height =
        static_cast<std::uint32_t>(
            height
        );

    // Some codecs do not report a valid FPS.
    // Use 30 FPS as a safe fallback.
    m_info.frameRate =
        fps > 0.0
            ? fps
            : 30.0;

    std::cout
        << "Local video opened successfully.\n"
        << "  Name: "
        << m_info.name
        << '\n'
        << "  Resolution: "
        << m_info.width
        << "x"
        << m_info.height
        << '\n'
        << "  FPS: "
        << m_info.frameRate
        << '\n';

    return true;
}

void LocalVideoSource::closeVideo()
{
    if (m_capture != nullptr)
    {
        m_capture->release();

        m_capture.reset();
    }
}

void LocalVideoSource::pushFrame(
    Frame&& frame
)
{
    std::lock_guard<std::mutex> lock(
        m_queueMutex
    );

    if (m_frameQueue.size() >=
        MaxQueuedFrames)
    {
        m_frameQueue.pop_front();
    }

    m_frameQueue.push_back(
        std::move(frame)
    );

    m_queueCondition.notify_one();
}

void LocalVideoSource::decodeLoop()
{
    m_state.store(
        VideoSourceState::Running,
        std::memory_order_release
    );

    std::uint64_t sequence = 0;

    const double fps =
        m_info.frameRate > 0.0
            ? m_info.frameRate
            : 30.0;

    // ---------------------------------------------------------
    // TRUE PLAYBACK CLOCK
    // ---------------------------------------------------------
    //
    // Do NOT simply sleep for one frame duration after
    // decoding each frame.
    //
    // Instead, establish a playback clock and schedule
    // every frame against that clock.
    //
    // Example at 30 FPS:
    //
    // Frame 0 -> 0.000 s
    // Frame 1 -> 0.033 s
    // Frame 2 -> 0.066 s
    // Frame 3 -> 0.100 s
    //
    // Time spent decoding a frame is therefore included
    // in the timing rather than being added on top.
    // ---------------------------------------------------------

    const auto frameDuration =
        std::chrono::duration<double>(
            1.0 / fps
        );

    const auto playbackStart =
        std::chrono::steady_clock::now();

    std::uint64_t frameNumber = 0;

    while (!m_stopRequested.load(
        std::memory_order_acquire))
    {
        cv::Mat frame;

        if (!m_capture->read(frame))
        {
            // Normal end of video.
            break;
        }

        if (frame.empty())
        {
            continue;
        }

        if (frame.type() != CV_8UC3)
        {
            cv::Mat converted;

            if (frame.channels() == 1)
            {
                cv::cvtColor(
                    frame,
                    converted,
                    cv::COLOR_GRAY2BGR
                );
            }
            else if (frame.channels() == 4)
            {
                cv::cvtColor(
                    frame,
                    converted,
                    cv::COLOR_BGRA2BGR
                );
            }
            else
            {
                continue;
            }

            frame =
                std::move(converted);
        }

        auto buffer =
            std::make_shared<FrameBuffer>();

        const std::size_t bytes =
            frame.total()
            * frame.elemSize();

        buffer->data.resize(
            bytes
        );

        if (frame.isContinuous())
        {
            std::memcpy(
                buffer->data.data(),
                frame.data,
                bytes
            );
        }
        else
        {
            const std::size_t rowBytes =
                static_cast<std::size_t>(
                    frame.cols
                )
                * frame.elemSize();

            for (int row = 0;
                 row < frame.rows;
                 ++row)
            {
                std::memcpy(
                    buffer->data.data()
                        +
                        static_cast<std::size_t>(
                            row
                        )
                        * rowBytes,

                    frame.ptr(row),

                    rowBytes
                );
            }
        }

        Frame output;

        output.buffer =
            std::move(buffer);

        output.width =
            static_cast<std::uint32_t>(
                frame.cols
            );

        output.height =
            static_cast<std::uint32_t>(
                frame.rows
            );

        output.stride =
            static_cast<std::uint32_t>(
                frame.cols
                * frame.elemSize()
            );

        output.format =
            PixelFormat::BGR8;

        output.timestampUs =
            static_cast<std::int64_t>(
                m_capture->get(
                    cv::CAP_PROP_POS_MSEC
                )
                * 1000.0
            );

        output.sequence =
            sequence++;

        pushFrame(
            std::move(output)
        );

        ++frameNumber;

        // -----------------------------------------------------
        // Schedule the next frame against the playback clock.
        // -----------------------------------------------------

        const auto targetTime =
            playbackStart
            +
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration
            >(
                frameDuration
                *
                static_cast<double>(
                    frameNumber
                )
            );

        const auto now =
            std::chrono::steady_clock::now();

        if (targetTime > now)
        {
            std::this_thread::sleep_until(
                targetTime
            );
        }

        // If targetTime is already in the past,
        // we do NOT sleep.
        //
        // This means decoding cannot deliberately
        // slow the video down.
    }

    // If the user requested stop(), stop() will
    // finalize the source state.
    //
    // Otherwise this was a natural end-of-video.
    if (!m_stopRequested.load(
        std::memory_order_acquire))
    {
        m_state.store(
            VideoSourceState::Stopped,
            std::memory_order_release
        );
    }
}