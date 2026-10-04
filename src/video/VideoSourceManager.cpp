#include "VideoSourceManager.hpp"

#include "CameraVideoSource.hpp"
#include "LocalVideoSource.hpp"
#include "RTSPVideoSource.hpp"

#include "../mobile/MobileStreamServer.hpp"
#include "../mobile/MobileStreamSource.hpp"

#include <utility>

VideoSourceManager::VideoSourceManager()
    : m_mobileStreamServer(
        std::make_unique<MobileStreamServer>()
    )
{
    m_mobileStreamServer->start(
        8443
    );
}

VideoSourceManager::~VideoSourceManager()
{
    stopAll();

    m_mobileStreamServer.reset();
}

// ---------------------------------------------------------
// Local video
// ---------------------------------------------------------

std::string VideoSourceManager::addLocalVideo(
    const std::string& filePath,
    const std::string& displayName
)
{
    if (filePath.empty())
    {
        return {};
    }

    const std::string sourceId =
        generateSourceId();

    auto source =
        std::make_unique<LocalVideoSource>(
            filePath,
            displayName
        );

    {
        std::lock_guard<std::mutex> lock(
            m_mutex
        );

        m_sources.emplace(
            sourceId,
            std::move(source)
        );
    }

    return sourceId;
}

// ---------------------------------------------------------
// Laptop camera
// ---------------------------------------------------------

std::string VideoSourceManager::addCamera(
    int cameraIndex,
    const std::string& displayName
)
{
    if (cameraIndex < 0)
    {
        return {};
    }

    const std::string sourceId =
        generateSourceId();

    auto source =
        std::make_unique<CameraVideoSource>(
            cameraIndex,
            displayName
        );

    {
        std::lock_guard<std::mutex> lock(
            m_mutex
        );

        m_sources.emplace(
            sourceId,
            std::move(source)
        );
    }

    return sourceId;
}

// ---------------------------------------------------------
// RTSP
// ---------------------------------------------------------

std::string VideoSourceManager::addRTSPStream(
    const std::string& url,
    const std::string& displayName
)
{
    if (url.empty())
    {
        return {};
    }

    const std::string sourceId =
        generateSourceId();

    auto source =
        std::make_unique<RTSPVideoSource>(
            url,
            displayName
        );

    {
        std::lock_guard<std::mutex> lock(
            m_mutex
        );

        m_sources.emplace(
            sourceId,
            std::move(source)
        );
    }

    return sourceId;
}

// ---------------------------------------------------------
// Mobile Stream
// ---------------------------------------------------------

std::string VideoSourceManager::addMobileStream(
    const std::string& displayName
)
{
    if (
        m_mobileStreamServer == nullptr
    )
    {
        return {};
    }

    const std::string sourceId =
        generateSourceId();

    auto source =
        std::make_unique<MobileStreamSource>(
            *m_mobileStreamServer,
            sourceId,
            displayName
        );

    {
        std::lock_guard<std::mutex> lock(
            m_mutex
        );

        m_sources.emplace(
            sourceId,
            std::move(source)
        );
    }

    return sourceId;
}

std::string VideoSourceManager::getMobileStreamUrl(
    const std::string& sourceId
) const
{
    if (
        m_mobileStreamServer == nullptr
    )
    {
        return {};
    }

    const auto urls =
        m_mobileStreamServer->localUrls(
            sourceId
        );

    if (urls.empty())
    {
        return {};
    }

    return urls.front();
}

bool VideoSourceManager::isMobileStreamServerRunning()
    const noexcept
{
    if (
        m_mobileStreamServer == nullptr
    )
    {
        return false;
    }

    return m_mobileStreamServer->isRunning();
}

MobileNetworkStats
VideoSourceManager::getMobileNetworkStats(
    const std::string& sourceId
) const noexcept
{
    if (
        m_mobileStreamServer == nullptr
    )
    {
        return {};
    }

    return
        m_mobileStreamServer->getNetworkStats(
            sourceId
        );
}

// ---------------------------------------------------------
// Generic source lifecycle
// ---------------------------------------------------------

bool VideoSourceManager::startSource(
    const std::string& sourceId
)
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    const auto iterator =
        m_sources.find(
            sourceId
        );

    if (
        iterator ==
        m_sources.end()
    )
    {
        return false;
    }

    return iterator->second->start();
}

bool VideoSourceManager::replayLocalVideo(
    const std::string& sourceId
)
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    const auto iterator =
        m_sources.find(
            sourceId
        );

    if (
        iterator ==
        m_sources.end()
    )
    {
        return false;
    }

    auto* localVideo =
        dynamic_cast<LocalVideoSource*>(
            iterator->second.get()
        );

    if (
        localVideo == nullptr
    )
    {
        return false;
    }

    return localVideo->restart();
}

void VideoSourceManager::stopSource(
    const std::string& sourceId
) noexcept
{
    VideoSource* source = nullptr;

    {
        std::lock_guard<std::mutex> lock(
            m_mutex
        );

        const auto iterator =
            m_sources.find(
                sourceId
            );

        if (
            iterator ==
            m_sources.end()
        )
        {
            return;
        }

        source =
            iterator->second.get();
    }

    if (
        source != nullptr
    )
    {
        source->stop();
    }
}

void VideoSourceManager::stopAll() noexcept
{
    std::vector<VideoSource*> sources;

    {
        std::lock_guard<std::mutex> lock(
            m_mutex
        );

        sources.reserve(
            m_sources.size()
        );

        for (
            auto& [id, source] :
            m_sources
        )
        {
            if (
                source != nullptr
            )
            {
                sources.push_back(
                    source.get()
                );
            }
        }
    }

    for (
        VideoSource* source :
        sources
    )
    {
        if (
            source != nullptr
        )
        {
            source->stop();
        }
    }
}

// ---------------------------------------------------------
// Frames
// ---------------------------------------------------------

bool VideoSourceManager::tryGetFrame(
    const std::string& sourceId,
    Frame& frame
)
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    const auto iterator =
        m_sources.find(
            sourceId
        );

    if (
        iterator ==
        m_sources.end()
    )
    {
        return false;
    }

    return iterator->second->tryGetFrame(
        frame
    );
}

// ---------------------------------------------------------
// Information
// ---------------------------------------------------------

const VideoSourceInfo*
VideoSourceManager::getSourceInfo(
    const std::string& sourceId
) const noexcept
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    const auto iterator =
        m_sources.find(
            sourceId
        );

    if (
        iterator ==
        m_sources.end()
    )
    {
        return nullptr;
    }

    return &iterator->second->info();
}

VideoSourceState
VideoSourceManager::getSourceState(
    const std::string& sourceId
) const noexcept
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    const auto iterator =
        m_sources.find(
            sourceId
        );

    if (
        iterator ==
        m_sources.end()
    )
    {
        return VideoSourceState::Error;
    }

    return iterator->second->state();
}

// ---------------------------------------------------------
// Source management
// ---------------------------------------------------------

bool VideoSourceManager::removeSource(
    const std::string& sourceId
) noexcept
{
    std::unique_ptr<VideoSource> source;

    {
        std::lock_guard<std::mutex> lock(
            m_mutex
        );

        const auto iterator =
            m_sources.find(
                sourceId
            );

        if (
            iterator ==
            m_sources.end()
        )
        {
            return false;
        }

        source =
            std::move(
                iterator->second
            );

        m_sources.erase(
            iterator
        );
    }

    if (
        source != nullptr
    )
    {
        source->stop();
    }

    return true;
}

std::size_t
VideoSourceManager::sourceCount()
    const noexcept
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    return m_sources.size();
}

std::vector<std::string>
VideoSourceManager::sourceIds() const
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    std::vector<std::string> ids;

    ids.reserve(
        m_sources.size()
    );

    for (
        const auto& [id, source] :
        m_sources
    )
    {
        ids.push_back(id);
    }

    return ids;
}

// ---------------------------------------------------------
// Source ID generation
// ---------------------------------------------------------

std::string
VideoSourceManager::generateSourceId()
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    return
        "source_" +
        std::to_string(
            m_nextSourceNumber++
        );
}