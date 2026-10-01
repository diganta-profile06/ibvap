#pragma once

#include "../../src/video/VideoSource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class WebVideoSourceManager
{
public:
    WebVideoSourceManager();

    ~WebVideoSourceManager();

    WebVideoSourceManager(
        const WebVideoSourceManager&
    ) = delete;

    WebVideoSourceManager& operator=(
        const WebVideoSourceManager&
    ) = delete;

    // ---------------------------------------------------------
    // Local video
    // ---------------------------------------------------------

    std::string addLocalVideo(
        const std::string& filePath,
        const std::string& displayName
    );

    // ---------------------------------------------------------
    // Laptop camera
    // ---------------------------------------------------------

    std::string addCamera(
        int cameraIndex,
        const std::string& displayName
    );

    // ---------------------------------------------------------
    // RTSP
    // ---------------------------------------------------------

    std::string addRTSPStream(
        const std::string& url,
        const std::string& displayName
    );

    // ---------------------------------------------------------
    // Mobile Stream
    // ---------------------------------------------------------

    std::string addMobileStream(
        const std::string& displayName
    );

    std::string getMobileStreamUrl(
        const std::string& sourceId
    ) const;

    bool isMobileStreamServerRunning()
        const noexcept;

    // ---------------------------------------------------------
    // Generic source lifecycle
    // ---------------------------------------------------------

    bool startSource(
        const std::string& sourceId
    );

    bool replayLocalVideo(
        const std::string& sourceId
    );

    void stopSource(
        const std::string& sourceId
    ) noexcept;

    void stopAll() noexcept;

    // ---------------------------------------------------------
    // Frames
    // ---------------------------------------------------------

    bool tryGetFrame(
        const std::string& sourceId,
        Frame& frame
    );

    // ---------------------------------------------------------
    // Information
    // ---------------------------------------------------------

    const VideoSourceInfo* getSourceInfo(
        const std::string& sourceId
    ) const noexcept;

    VideoSourceState getSourceState(
        const std::string& sourceId
    ) const noexcept;

    // ---------------------------------------------------------
    // Source management
    // ---------------------------------------------------------

    bool removeSource(
        const std::string& sourceId
    ) noexcept;

    std::size_t sourceCount()
        const noexcept;

    std::vector<std::string>
    sourceIds() const;

private:
    std::string generateSourceId();

private:
    mutable std::mutex m_mutex;

    std::unordered_map<
        std::string,
        std::unique_ptr<VideoSource>
    > m_sources;

    std::uint64_t
        m_nextSourceNumber = 1;
};