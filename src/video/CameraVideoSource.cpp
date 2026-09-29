#include "CameraVideoSource.hpp"

#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

CameraVideoSource::CameraVideoSource(
    int cameraIndex,
    std::string displayName
)
    : m_cameraIndex(cameraIndex)
{
    m_info.id =
        "camera_" +
        std::to_string(
            m_cameraIndex
        );

    m_info.name =
        std::move(displayName);

    m_info.type =
        VideoSourceType::LaptopCamera;
}

CameraVideoSource::~CameraVideoSource()
{
    stop();
}

bool CameraVideoSource::start()
{
    VideoSourceState expected =
        VideoSourceState::Created;

    if (!m_state.compare_exchange_strong(
            expected,
            VideoSourceState::Starting))
    {
        return false;
    }

    if (m_captureThread.joinable())
    {
        m_captureThread.join();
    }

    m_stopRequested.store(
        false,
        std::memory_order_release
    );

    if (!openCamera())
    {
        m_state.store(
            VideoSourceState::Error,
            std::memory_order_release
        );

        return false;
    }

    m_captureThread =
        std::thread(
            &CameraVideoSource::captureLoop,
            this
        );

    return true;
}

void CameraVideoSource::stop() noexcept
{
    m_stopRequested.store(
        true,
        std::memory_order_release
    );

    m_queueCondition.notify_all();

    if (m_captureThread.joinable())
    {
        m_captureThread.join();
    }

    closeCamera();

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
CameraVideoSource::state()
    const noexcept
{
    return m_state.load(
        std::memory_order_acquire
    );
}

bool CameraVideoSource::tryGetFrame(
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

    /*
     * For live video we always consume the
     * newest available frame.
     *
     * This prevents old frames from accumulating
     * and creating visible latency.
     */
    frame =
        std::move(
            m_frameQueue.back()
        );

    m_frameQueue.clear();

    return frame.valid();
}

const VideoSourceInfo&
CameraVideoSource::info()
    const noexcept
{
    return m_info;
}

bool CameraVideoSource::openCamera()
{
    m_capture =
        std::make_unique<cv::VideoCapture>(
            m_cameraIndex,
            cv::CAP_ANY
        );

    if (!m_capture->isOpened())
    {
        std::cerr
            << "CameraVideoSource: "
               "Failed to open camera index "
            << m_cameraIndex
            << ".\n";

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

    m_info.width =
        width > 0.0
            ? static_cast<std::uint32_t>(
                  width
              )
            : 0;

    m_info.height =
        height > 0.0
            ? static_cast<std::uint32_t>(
                  height
              )
            : 0;

    m_info.frameRate =
        fps > 0.0
            ? fps
            : 30.0;

    std::cout
        << "Laptop camera opened successfully.\n"
        << "  Name: "
        << m_info.name
        << '\n'
        << "  Camera index: "
        << m_cameraIndex
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

void CameraVideoSource::closeCamera()
{
    if (m_capture != nullptr)
    {
        m_capture->release();

        m_capture.reset();
    }
}

void CameraVideoSource::pushFrame(
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

void CameraVideoSource::captureLoop()
{
    m_state.store(
        VideoSourceState::Running,
        std::memory_order_release
    );

    std::uint64_t sequence = 0;

    while (!m_stopRequested.load(
        std::memory_order_acquire))
    {
        cv::Mat frame;

        if (!m_capture->read(frame))
        {
            if (!m_stopRequested.load(
                    std::memory_order_acquire))
            {
                std::cerr
                    << "CameraVideoSource: "
                       "Failed to capture frame.\n";
            }

            break;
        }

        if (frame.empty())
        {
            continue;
        }

        /*
         * -----------------------------------------------------
         * CAMERA ORIENTATION
         * -----------------------------------------------------
         *
         * The laptop webcam is currently presenting a mirrored
         * image. Flip it horizontally before the frame enters
         * the common IBVAP frame pipeline.
         *
         * flipCode = 1 means horizontal flip.
         *
         * This is intentionally done here rather than in the UI
         * so the future AI pipeline also receives the correct
         * real-world orientation.
         */
        cv::flip(
            frame,
            frame,
            1
        );

        /*
         * OpenCV cameras normally provide BGR.
         *
         * Normalize only when necessary so the rest of the
         * IBVAP frame pipeline receives a predictable format.
         */
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

        /*
         * Camera frames are live rather than
         * file-timestamped.
         *
         * Use a monotonic clock for the timestamp.
         */
        const auto now =
            std::chrono::steady_clock::now();

        const auto timestamp =
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(
                now.time_since_epoch()
            );

        output.timestampUs =
            timestamp.count();

        output.sequence =
            sequence++;

        pushFrame(
            std::move(output)
        );
    }

    if (!m_stopRequested.load(
        std::memory_order_acquire))
    {
        m_state.store(
            VideoSourceState::Error,
            std::memory_order_release
        );
    }
}