#include "RTSPVideoSource.hpp"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <utility>

RTSPVideoSource::RTSPVideoSource(
    const std::string& url,
    const std::string& displayName
)
    : m_url(url)
{
    m_info.name = displayName;

    m_info.type =
        VideoSourceType::RTSP;

    m_info.address = url;

    m_info.deviceConnected = false;
}

RTSPVideoSource::~RTSPVideoSource()
{
    stop();
}

bool RTSPVideoSource::start()
{
    {
        std::lock_guard<std::mutex> lock(
            m_stateMutex
        );

        if (
            m_state ==
                VideoSourceState::Running ||
            m_state ==
                VideoSourceState::Starting
        )
        {
            return true;
        }

        m_state =
            VideoSourceState::Starting;
    }

    m_stopRequested = false;

    m_worker =
        std::thread(
            &RTSPVideoSource::captureLoop,
            this
        );

    return true;
}

void RTSPVideoSource::stop() noexcept
{
    m_stopRequested = true;

    if (m_worker.joinable())
    {
        m_worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(
            m_stateMutex
        );

        m_state =
            VideoSourceState::Stopped;
    }

    m_info.deviceConnected = false;
}

VideoSourceState
RTSPVideoSource::state()
    const noexcept
{
    std::lock_guard<std::mutex> lock(
        m_stateMutex
    );

    return m_state;
}

bool RTSPVideoSource::tryGetFrame(
    Frame& frame
)
{
    std::lock_guard<std::mutex> lock(
        m_frameMutex
    );

    if (!m_latestFrame.valid())
    {
        return false;
    }

    frame =
        std::move(m_latestFrame);

    m_latestFrame = {};

    return true;
}

const VideoSourceInfo&
RTSPVideoSource::info()
    const noexcept
{
    return m_info;
}

void RTSPVideoSource::setState(
    VideoSourceState newState
)
{
    std::lock_guard<std::mutex> lock(
        m_stateMutex
    );

    m_state = newState;
}

void RTSPVideoSource::captureLoop()
{
    cv::VideoCapture capture;

    capture.open(
        m_url,
        cv::CAP_FFMPEG
    );

    if (!capture.isOpened())
    {
        std::cerr
            << "RTSP: Failed to open stream: "
            << m_url
            << '\n';

        m_info.deviceConnected =
            false;

        setState(
            VideoSourceState::Error
        );

        return;
    }

    m_info.deviceConnected = true;

    m_info.width =
        static_cast<std::uint32_t>(
            capture.get(
                cv::CAP_PROP_FRAME_WIDTH
            )
        );

    m_info.height =
        static_cast<std::uint32_t>(
            capture.get(
                cv::CAP_PROP_FRAME_HEIGHT
            )
        );

    m_info.frameRate =
        capture.get(
            cv::CAP_PROP_FPS
        );

    setState(
        VideoSourceState::Running
    );

    std::cout
        << "RTSP connected: "
        << m_url
        << '\n';

    cv::Mat image;

    while (!m_stopRequested)
    {
        if (!capture.read(image))
        {
            if (m_stopRequested)
            {
                break;
            }

            std::cerr
                << "RTSP: Frame read failed: "
                << m_url
                << '\n';

            break;
        }

        if (image.empty())
        {
            continue;
        }

        if (image.channels() != 3)
        {
            continue;
        }

        auto buffer =
            std::make_shared<FrameBuffer>();

        const std::size_t rowBytes =
            static_cast<std::size_t>(
                image.cols
            ) *
            3;

        buffer->data.resize(
            rowBytes *
            static_cast<std::size_t>(
                image.rows
            )
        );

        for (int row = 0;
             row < image.rows;
             ++row)
        {
            const auto* source =
                image.ptr<std::uint8_t>(
                    row
                );

            auto* destination =
                buffer->data.data() +
                (
                    static_cast<std::size_t>(
                        row
                    ) *
                    rowBytes
                );

            std::copy(
                source,
                source + rowBytes,
                destination
            );
        }

        Frame frame;

        frame.buffer = buffer;

        frame.width =
            static_cast<std::uint32_t>(
                image.cols
            );

        frame.height =
            static_cast<std::uint32_t>(
                image.rows
            );

        frame.stride =
            static_cast<std::uint32_t>(
                rowBytes
            );

        frame.format =
            PixelFormat::BGR8;

        frame.timestampUs =
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
            ).count();

        frame.sequence =
            ++m_sequence;

        {
            std::lock_guard<std::mutex> lock(
                m_frameMutex
            );

            m_latestFrame =
                std::move(frame);
        }

        m_info.width =
            static_cast<std::uint32_t>(
                image.cols
            );

        m_info.height =
            static_cast<std::uint32_t>(
                image.rows
            );
    }

    capture.release();

    m_info.deviceConnected =
        false;

    if (m_stopRequested)
    {
        setState(
            VideoSourceState::Stopped
        );
    }
    else
    {
        setState(
            VideoSourceState::Error
        );
    }

    std::cout
        << "RTSP disconnected: "
        << m_url
        << '\n';
}