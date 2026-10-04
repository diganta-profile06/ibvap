#pragma once

#include "../../src/video/VideoSource.hpp"

#include <cstdint>
#include <memory>
#include <string>

class WebVideoSource final : public VideoSource
{
public:

    WebVideoSource(
        VideoSourceType type,
        std::string sourceId,
        std::string displayName,
        std::string sourceUrl = {}
    );

    ~WebVideoSource() override;

    WebVideoSource(
        const WebVideoSource&
    ) = delete;

    WebVideoSource& operator=(
        const WebVideoSource&
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

    mutable VideoSourceInfo
        m_info;

    mutable VideoSourceState
        m_state =
            VideoSourceState::Created;

    std::string
        m_sourceUrl;

    std::shared_ptr<FrameBuffer>
        m_frameBuffer;

    std::uint64_t
        m_sequence = 0;
};