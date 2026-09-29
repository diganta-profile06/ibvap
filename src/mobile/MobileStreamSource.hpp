#pragma once

#include "../video/VideoSource.hpp"

#include <cstdint>
#include <string>

class MobileStreamServer;

class MobileStreamSource final
    : public VideoSource
{
public:
    MobileStreamSource(
        MobileStreamServer& server,
        const std::string& streamId,
        const std::string& displayName
    );

    ~MobileStreamSource() override;

    MobileStreamSource(
        const MobileStreamSource&
    ) = delete;

    MobileStreamSource& operator=(
        const MobileStreamSource&
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
    MobileStreamServer& m_server;

    std::string m_streamId;

    VideoSourceInfo m_info;

    VideoSourceState m_state =
        VideoSourceState::Created;

    std::uint64_t m_lastSequence = 0;
};