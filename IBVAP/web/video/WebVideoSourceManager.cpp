#include "WebVideoSourceManager.hpp"

#include "WebVideoSource.hpp"

#include <utility>

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include <cstdlib>
#include <string>

namespace
{

EM_JS(
    char*,
    ibvap_get_mobile_base_url,
    (),
    {
        let baseUrl = "";

        if (
            typeof window !== "undefined" &&
            window.location
        )
        {
            const hostname =
                window.location.hostname;

            if (
                hostname &&
                hostname !== "null"
            )
            {
                /*
                 * The WASM application may be served from
                 * another HTTP port, for example:
                 *
                 * http://192.168.0.108:8080
                 *
                 * The native IBVAP MobileStreamServer uses:
                 *
                 * https://192.168.0.108:8443
                 *
                 * Therefore use the browser's actual hostname
                 * but explicitly target the native HTTPS server.
                 */
                baseUrl =
                    "https://" +
                    hostname +
                    ":8443";
            }
        }

        if (
            !baseUrl
        )
        {
            baseUrl = "";
        }

        return stringToNewUTF8(
            baseUrl
        );
    }
);


std::string getBrowserOrigin()
{
    char* origin =
        ibvap_get_mobile_base_url();

    if (
        origin == nullptr
    )
    {
        return {};
    }

    std::string result =
        origin;

    std::free(
        origin
    );

    return result;
}

}

#endif


WebVideoSourceManager::WebVideoSourceManager()
{
}


WebVideoSourceManager::~WebVideoSourceManager()
{
    stopAll();
}


std::string WebVideoSourceManager::addLocalVideo(
    const std::string& filePath,
    const std::string& displayName
)
{
    if (
        filePath.empty()
    )
    {
        return {};
    }

    const std::string sourceId =
        generateSourceId();

    auto source =
        std::make_unique<WebVideoSource>(
            VideoSourceType::LocalVideo,
            sourceId,
            displayName,
            filePath
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


std::string WebVideoSourceManager::addCamera(
    int cameraIndex,
    const std::string& displayName
)
{
    if (
        cameraIndex < 0
    )
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


std::string WebVideoSourceManager::addRTSPStream(
    const std::string& url,
    const std::string& displayName
)
{
    if (
        url.empty()
    )
    {
        return {};
    }

    const std::string sourceId =
        generateSourceId();

    auto source =
        std::make_unique<WebVideoSource>(
            VideoSourceType::RTSP,
            sourceId,
            displayName,
            url
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
    if (
        sourceId.empty()
    )
    {
        return {};
    }

#ifdef __EMSCRIPTEN__

    /*
     * WASM does not contain the native Crow
     * MobileStreamServer.
     *
     * The actual mobile camera endpoint is provided
     * by the native IBVAP application on HTTPS port 8443.
     *
     * The browser hostname is used so we do not hard-code
     * the laptop's LAN address.
     *
     * Example:
     *
     * WASM opened at:
     *
     * http://192.168.0.108:8080/IBVAP.html
     *
     * Generated mobile URL:
     *
     * https://192.168.0.108:8443/mobile?source=source_1
     *
     * This is the address the phone must use.
     */
    const std::string origin =
        getBrowserOrigin();

    if (
        origin.empty()
    )
    {
        return
            "/mobile?source=" +
            sourceId;
    }

    return
        origin +
        "/mobile?source=" +
        sourceId;

#else

    (void)sourceId;

    return {};

#endif
}


bool WebVideoSourceManager::isMobileStreamServerRunning()
    const noexcept
{
#ifdef __EMSCRIPTEN__

    /*
     * The native MobileStreamServer runs outside
     * the WASM module.
     *
     * The WASM UI is therefore allowed to create
     * Mobile Camera slots.
     *
     * The actual phone transport is handled by the
     * native IBVAP MobileStreamServer and the browser
     * viewer implementation.
     */
    return true;

#else

    return false;

#endif
}


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
    /*
     * The browser-side WebVideoSource keeps the
     * selected video URL internally.
     *
     * Restarting the source resets the HTML5
     * video element back to the beginning.
     */
    return startSource(
        sourceId
    );
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