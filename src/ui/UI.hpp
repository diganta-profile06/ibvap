#pragma once

#ifdef __EMSCRIPTEN__

#include "../../web/video/WebVideoSourceManager.hpp"

using IBVAPVideoSourceManager =
    WebVideoSourceManager;

#else

#include "../video/VideoSourceManager.hpp"

using IBVAPVideoSourceManager =
    VideoSourceManager;

#endif

#include "../ai/AIResult.hpp"
#include "../database/Database.hpp"

#include <SDL3/SDL.h>

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>


class AIEngine;


class IBVAPUI
{
public:
    IBVAPUI(
        SDL_Window* window,
        IBVAPVideoSourceManager& videoSourceManager
    );

    ~IBVAPUI();

    IBVAPUI(const IBVAPUI&) = delete;
    IBVAPUI& operator=(
        const IBVAPUI&
    ) = delete;

    void initialize();
    void render();
    void shutdown();

private:
    struct VideoSlot
    {
        std::string name;
        std::string type;
        std::string sourceId;
        std::string filePath;
        std::string mobileUrl;

        bool hasVideo = false;
        bool videoError = false;

        /*
         * Mobile-camera QR state.
         */
        std::vector<std::uint8_t> qrModules;

        std::uint8_t qrVersion = 0;
        std::uint8_t qrSize = 0;

        bool qrReady = false;

        /*
         * Latest completed AI result.
         *
         * These values are owned by the UI/render thread.
         * The AI worker never writes directly into VideoSlot.
         */
        AIResult aiResult;

        bool aiAvailable = false;
        bool aiError = false;

        /*
         * Number of frames submitted to the AI worker.
         */
        std::uint64_t aiFrameCounter = 0;

        /*
         * Generation of the latest AI result already consumed
         * by this slot.
         */
        std::uint64_t aiResultGeneration = 0;

        /*
         * Exact Frame::sequence currently displayed by the
         * video texture for this slot.
         *
         * This is used to prevent an AI result from an older
         * frame from being drawn over a newer frame.
         */
        std::uint64_t displayedFrameSequence = 0;

        /*
         * Exact Frame::sequence belonging to aiResult.
         *
         * The overlay is allowed to draw only when:
         *
         *     aiResultFrameSequence ==
         *     displayedFrameSequence
         *
         * This keeps detections synchronized with moving
         * persons and vehicles without introducing a tracker
         * or delaying video playback.
         */
        std::uint64_t aiResultFrameSequence = 0;

        std::int64_t aiLastTimestampUs = 0;

        /*
         * Stage-1 dashboard state.
         *
         * Dashboard detections are deliberately kept inside the UI
         * layer.  No database and no tracker are required here.
         * A detection becomes visible only after 2 consecutive
         * matching AI results with confidence >= 40 percent.
         */
        struct DashboardDetection
        {
            AIObjectClass classId = AIObjectClass::Unknown;
            float confidence = 0.0f;

            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;

            std::uint32_t stableFrames = 0;
            std::uint64_t lastFrameSequence = 0;
            std::int64_t lastDetectedAtUs = 0;

            unsigned int texture = 0;
            std::uint32_t textureWidth = 0;
            std::uint32_t textureHeight = 0;

            bool visible = false;

            DatabaseImage image;
            DatabaseImage faceImage;
            std::int64_t historyId = 0;
            std::int64_t identityId = 0;
            DatabaseIdentityType identityType = DatabaseIdentityType::Ally;
            std::string identityName;
            bool historySaved = false;
        };

        std::vector<DashboardDetection>
            dashboardDetections;

        struct FenceDetectionState
        {
            AIObjectClass classId = AIObjectClass::Unknown;
            float centerX = 0.0f;
            float centerY = 0.0f;
            bool inside = false;
        };

        bool hasVirtualFence = false;
        bool fencePlacementMode = false;
        bool fenceDrawing = false;
        float fenceX = 0.0f;
        float fenceY = 0.0f;
        float fenceWidth = 0.0f;
        float fenceHeight = 0.0f;
        float fenceStartX = 0.0f;
        float fenceStartY = 0.0f;
        float previewFenceX = 0.0f;
        float previewFenceY = 0.0f;
        float previewFenceWidth = 0.0f;
        float previewFenceHeight = 0.0f;
        std::vector<FenceDetectionState> previousFenceDetections;
    };

    struct AlertToast
    {
        std::string message;
        std::uint32_t color = 0;
        std::chrono::steady_clock::time_point createdAt;
    };

    struct VideoTexture
    {
        unsigned int texture = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

#ifndef __EMSCRIPTEN__

    /*
     * =========================================================
     * NATIVE AI WORKER
     * =========================================================
     *
     * The render thread submits only the newest frame for
     * each source.
     *
     * There is deliberately NO unbounded AI queue.
     *
     * If AI is still processing frame A and frames B/C/D
     * arrive, only D needs to survive.
     *
     * This keeps AI latency bounded instead of allowing
     * inference to make the video progressively fall behind.
     */

    struct AIWorkerResult
    {
        AIResult result;

        bool available = false;
        bool error = false;

        std::uint64_t generation = 0;

        /*
         * Timestamp belonging to the exact frame analyzed.
         */
        std::int64_t timestampUs = 0;

        /*
         * Exact Frame::sequence belonging to the AI result.
         *
         * This is the synchronization key between the
         * asynchronous AI worker and the displayed video.
         */
        std::uint64_t frameSequence = 0;

        /* Exact frame that produced the result. */
        Frame frame;
    };

#endif

private:
    void setupStyle();

    void renderTopBar();
    void renderWorkspace();
    void renderLeftPanel();
    void renderVideoArea();
    void renderRightPanel();
    void renderDashboard();
    void renderHistory();
    void renderBottomBar();
    void renderAlertToasts();
    void queueAlert(
        const std::string& message,
        const std::string& sourceId,
        std::uint32_t color
    );

    void renderVideoSlot(
        std::size_t slotIndex,
        bool fullscreen
    );

    void renderVideoPlaceholder(
        const VideoSlot& slot
    );

    void renderMobileNetworkPanel(
        const VideoSlot& slot
    );

    void renderAIOverlay(
        VideoSlot& slot
    );

    void renderAIDetections(
        VideoSlot& slot
    );

    void renderAIStatus(
        const VideoSlot& slot
    );

    void renderAddSourcePopup();

    void updateVideoTextures();

    bool uploadFrameToTexture(
        const std::string& sourceId,
        const Frame& frame
    );

    void cleanupUnusedTextures();

    void removeVideoSlot(
        std::size_t slotIndex
    );

    bool createRTSPSource(
        std::size_t slotIndex
    );

    bool createCameraSource(
        std::size_t slotIndex
    );

    bool createMobileSource(
        std::size_t slotIndex
    );

    void requestVideoFile(
        std::size_t slotIndex
    );

    void copyMobileUrl(
        const std::string& url
    );

    bool generateMobileQrCode(
        VideoSlot& slot
    );

    void renderMobileQrCode(
        const VideoSlot& slot,
        float maximumSize
    );

    /*
     * =========================================================
     * AI
     * =========================================================
     */

    bool initializeAI();

    void shutdownAI();

    void updateAI();

    bool runAIForSlot(
        VideoSlot& slot,
        const Frame& frame
    );

    void saveDashboardDetectionToHistory(
        const VideoSlot& slot,
        VideoSlot::DashboardDetection& detection
    );

    void refreshHistory();
    void clearHistoryTextures() noexcept;
    void createIdentityFromSelectedHistory(
        DatabaseIdentityType type
    );

    void updateDashboardForSlot(
        VideoSlot& slot,
        const AIResult& result,
        const Frame& analyzedFrame
    );

    bool uploadDashboardCrop(
        VideoSlot::DashboardDetection& detection,
        const Frame& frame
    );

    void clearDashboardTextures(
        VideoSlot& slot
    ) noexcept;

#ifndef __EMSCRIPTEN__

    void startAIWorker();

    void stopAIWorker() noexcept;

    void aiWorkerLoop();

    void submitAIFrame(
        const std::string& sourceId,
        const Frame& frame
    );

    bool getLatestAIResult(
        const std::string& sourceId,
        AIResult& result,
        bool& available,
        bool& error,
        std::uint64_t& generation,
        std::int64_t& timestampUs,
        std::uint64_t& frameSequence,
        Frame& frame
    );

    void removeAISource(
        const std::string& sourceId
    );

#endif

    static void SDLCALL fileDialogCallback(
        void* userdata,
        const char* const* filelist,
        int filter
    );

    void processPendingFileSelection();

    bool captureImage(
        const Frame& frame,
        const AIBoundingBox& boundingBox,
        DatabaseImage& image,
        std::uint32_t outputWidth = 96,
        std::uint32_t outputHeight = 128
    ) const;

private:
    SDL_Window* m_window = nullptr;

    IBVAPVideoSourceManager&
        m_videoSourceManager;

    bool m_initialized = false;

    std::vector<VideoSlot>
        m_videoSlots;

    std::vector<AlertToast> m_alertToasts;
    std::unordered_map<
        std::string,
        std::chrono::steady_clock::time_point
    > m_alertCooldowns;

    std::unordered_map<
        std::string,
        VideoTexture
    > m_videoTextures;

    int m_selectedSlot = -1;
    int m_fullscreenSlot = -1;

    bool m_showHistory = false;

    std::unique_ptr<Database> m_database;
    std::vector<HistoryRecord> m_historyRecords;
    std::unordered_map<std::int64_t, unsigned int> m_historyTextures;
    std::int64_t m_selectedHistoryId = 0;
    bool m_showIdentityPopup = false;
    DatabaseIdentityType m_pendingIdentityType = DatabaseIdentityType::Ally;
    char m_identityName[256] = {};

    bool m_showAddSourcePopup = false;

    char m_sourceName[256] = {};

    /*
     * Source type indices:
     *
     * 0 = RTSP Stream
     * 1 = Laptop Camera
     * 2 = Pre-recorded video
     * 3 = Mobile Camera
     */
    int m_selectedSourceType = 0;

    char m_rtspUrl[1024] = {};

    std::mutex m_fileDialogMutex;

    bool m_fileDialogOpen = false;
    bool m_fileDialogResultReady = false;

    std::size_t m_pendingFileSlot = 0;

    std::string m_pendingFilePath;
    std::string m_fileDialogError;

    /*
     * AI engine.
     */
    std::unique_ptr<AIEngine> m_aiEngine;

    bool m_aiInitialized = false;

    std::string m_aiParamPath;
    std::string m_aiBinPath;

#ifndef __EMSCRIPTEN__

    /*
     * =========================================================
     * NATIVE AI WORKER STATE
     * =========================================================
     */

    std::thread m_aiWorkerThread;

    std::mutex m_aiWorkerMutex;

    std::condition_variable
        m_aiWorkerCondition;

    bool m_aiWorkerStopRequested = false;

    bool m_aiWorkerRunning = false;

    /*
     * Newest frame waiting to be processed for each source.
     */
    std::unordered_map<
        std::string,
        Frame
    > m_aiLatestFrames;

    /*
     * Source IDs currently waiting in m_aiReadySources.
     *
     * This prevents the same source from being inserted into
     * the ready queue repeatedly while it already has a pending
     * job.
     */
    std::unordered_set<
        std::string
    > m_aiPendingSources;

    /*
     * Small source-ID scheduling queue.
     *
     * It contains source IDs, not video frames.
     *
     * Therefore it cannot become a large frame backlog.
     */
    std::deque<
        std::string
    > m_aiReadySources;

    /*
     * Latest completed AI result for every source.
     */
    std::unordered_map<
        std::string,
        AIWorkerResult
    > m_aiResults;

    /*
     * Sources which are still owned by the UI.
     *
     * Prevents a worker that is finishing an old inference
     * from publishing a result after a source was removed.
     */
    std::unordered_set<
        std::string
    > m_aiActiveSources;

#endif
};
