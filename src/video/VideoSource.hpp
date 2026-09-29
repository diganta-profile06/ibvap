#pragma once

#include "Frame.hpp"

#include <cstdint>
#include <string>

enum class VideoSourceType
{
    Unknown,

    RTSP,

    LaptopCamera,

    LocalVideo,

    MobileStream
};

enum class VideoSourceState
{
    Created,

    WaitingForDevice,

    Ready,

    Starting,

    Running,

    Paused,

    Stopping,

    Stopped,

    Error
};

struct VideoSourceInfo
{
    std::string id;

    std::string name;

    VideoSourceType type =
        VideoSourceType::Unknown;

    std::uint32_t width = 0;

    std::uint32_t height = 0;

    double frameRate = 0.0;

    std::string address;

    bool deviceConnected = false;
};

class VideoSource
{
public:
    virtual ~VideoSource() = default;

    VideoSource(const VideoSource&) = delete;

    VideoSource& operator=(
        const VideoSource&
    ) = delete;

    virtual bool start() = 0;

    virtual void stop() noexcept = 0;

    virtual VideoSourceState state()
        const noexcept = 0;

    virtual bool isRunning()
        const noexcept
    {
        return
            state() ==
            VideoSourceState::Running;
    }

    virtual bool tryGetFrame(
        Frame& frame
    ) = 0;

    virtual const VideoSourceInfo&
    info() const noexcept = 0;

protected:
    VideoSource() = default;
};