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

    std::string addLocalVideo(
        const std::string& filePath,
        const std::string& displayName
    );

    std::string addCamera(
        int cameraIndex,
        const std::string& displayName
    );

    std::string addRTSPStream(
        const std::string& url,
        const std::string& displayName
    );

    std::string addMobileStream(
        const std::string& displayName
    );

    std::string getMobileStreamUrl(
        const std::string& sourceId
    ) const;

    bool isMobileStreamServerRunning()
        const noexcept;

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

    bool tryGetFrame(
        const std::string& sourceId,
        Frame& frame
    );

    const VideoSourceInfo* getSourceInfo(
        const std::string& sourceId
    ) const noexcept;

    VideoSourceState getSourceState(
        const std::string& sourceId
    ) const noexcept;

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