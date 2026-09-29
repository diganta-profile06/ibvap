#include "WebVideoSourceManager.hpp"

#include "WebVideoSource.hpp"

#include <utility>

WebVideoSourceManager::WebVideoSourceManager()
{
}

WebVideoSourceManager::~WebVideoSourceManager()
{
    stopAll();
}

// ---------------------------------------------------------
// Local video
// ---------------------------------------------------------

std::string WebVideoSourceManager::addLocalVideo(
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
        std::make_unique<WebVideoSource>(
            VideoSourceType::LocalVideo,
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

// ---------------------------------------------------------
// Laptop camera
// ---------------------------------------------------------

std::string WebVideoSourceManager::addCamera(
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
        std::make_unique<WebVideoSource>(
            VideoSourceType::LaptopCamera,
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

// ---------------------------------------------------------
// RTSP
// ---------------------------------------------------------

std::string WebVideoSourceManager::addRTSPStream(
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
        std::make_unique<WebVideoSource>(
            VideoSourceType::RTSP,
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

// ---------------------------------------------------------
// Mobile Stream
// ---------------------------------------------------------

std::string WebVideoSourceManager::addMobileStream(
    const std::string& displayName
)
{
    const std::string sourceId =
        generateSourceId();

    auto source =
        std::make_unique<WebVideoSource>(
            VideoSourceType::MobileStream,
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

std::string WebVideoSourceManager::getMobileStreamUrl(
    const std::string& sourceId
) const
{
    (void)sourceId;

    return {};
}

bool WebVideoSourceManager::isMobileStreamServerRunning()
    const noexcept
{
    return false;
}

// ---------------------------------------------------------
// Generic source lifecycle
// ---------------------------------------------------------

bool WebVideoSourceManager::startSource(
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

bool WebVideoSourceManager::replayLocalVideo(
    const std::string& sourceId
)
{
    (void)sourceId;

    return false;
}

void WebVideoSourceManager::stopSource(
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

void WebVideoSourceManager::stopAll() noexcept
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
            (void)id;

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

bool WebVideoSourceManager::tryGetFrame(
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
WebVideoSourceManager::getSourceInfo(
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
WebVideoSourceManager::getSourceState(
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

bool WebVideoSourceManager::removeSource(
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
WebVideoSourceManager::sourceCount()
    const noexcept
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );

    return m_sources.size();
}

std::vector<std::string>
WebVideoSourceManager::sourceIds() const
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
        (void)source;

        ids.push_back(
            id
        );
    }

    return ids;
}

// ---------------------------------------------------------
// Source ID generation
// ---------------------------------------------------------

std::string
WebVideoSourceManager::generateSourceId()
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