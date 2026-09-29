// ==================== PART 1 / 6 ====================

#include "UI.hpp"

#include "../ai/AIEngine.hpp"

#include "../video/VideoSource.hpp"

#include "../../external/qrcode/qrcode.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>


#ifdef __EMSCRIPTEN__

EM_JS(
    void,
    ibvap_web_open_video_file_picker,
    (),
    {
        let input =
            document.getElementById(
                "ibvap-web-video-file-input"
            );

        if (!input)
        {
            input =
                document.createElement("input");

            input.type =
                "file";

            input.id =
                "ibvap-web-video-file-input";

            input.accept =
                "video/mp4,video/webm,video/quicktime,video/x-matroska,video/*";

            input.style.display =
                "none";

            document.body.appendChild(
                input
            );
        }

        input.onchange =
            function()
            {
                if (
                    !input.files ||
                    input.files.length === 0
                )
                {
                    Module.IBVAPFileSelection =
                        null;

                    return;
                }

                const file =
                    input.files[0];

                const url =
                    URL.createObjectURL(
                        file
                    );

                Module.IBVAPFileSelection =
                {
                    name: file.name,
                    url: url
                };

                input.value =
                    "";
            };

        input.click();
    }
);

EM_JS(
    int,
    ibvap_web_poll_video_file,
    (
        char* nameOut,
        int nameCapacity,
        char* urlOut,
        int urlCapacity
    ),
    {
        if (
            !Module.IBVAPFileSelection
        )
        {
            return 0;
        }

        const selection =
            Module.IBVAPFileSelection;

        Module.IBVAPFileSelection =
            null;

        stringToUTF8(
            selection.name,
            nameOut,
            nameCapacity
        );

        stringToUTF8(
            selection.url,
            urlOut,
            urlCapacity
        );

        return 1;
    }
);

#endif


namespace
{

const char* sourceTypeName(
    VideoSourceType type
)
{
    switch (type)
    {
        case VideoSourceType::RTSP:
            return "RTSP";

        case VideoSourceType::LaptopCamera:
            return "Laptop Camera";

        case VideoSourceType::LocalVideo:
            return "Pre-recorded video";

        case VideoSourceType::MobileStream:
            return "Mobile Camera";

        default:
            return "Unknown";
    }
}


const char* aiObjectClassName(
    AIObjectClass classId
)
{
    switch (classId)
    {
        case AIObjectClass::Person:
            return "PERSON";

        case AIObjectClass::Vehicle:
            return "VEHICLE";

        case AIObjectClass::Weapon:
            return "WEAPON";

        case AIObjectClass::Face:
            return "FACE";

        case AIObjectClass::LicensePlate:
            return "PLATE";

        default:
            return "OBJECT";
    }
}


std::filesystem::path findAIModelPath(
    const std::filesystem::path& fileName
)
{
    /*
     * First try the executable/application directory.
     *
     * The native CMake build copies:
     *
     * models/detection/
     *
     * beside IBVAP.exe.
     *
     * SDL_GetBasePath() returns UTF-8 on the supported
     * platforms. Use std::filesystem::u8path() instead
     * of constructing std::filesystem::path directly
     * from the UTF-8 string.
     *
     * This avoids the MinGW/UCRT locale conversion issue
     * that can otherwise throw:
     *
     * filesystem_error:
     * Cannot convert character sequence
     */

    const char* basePath =
        SDL_GetBasePath();

    if (
        basePath != nullptr &&
        basePath[0] != '\0'
    )
    {
        try
        {
            const std::filesystem::path
                executablePath =
                    std::filesystem::u8path(
                        basePath
                    );

            const std::filesystem::path
                candidate =
                    executablePath /
                    "models" /
                    "detection" /
                    fileName;

            if (
                std::filesystem::exists(
                    candidate
                )
            )
            {
                return candidate;
            }
        }
        catch (
            const std::filesystem::filesystem_error&
        )
        {
            /*
             * If the executable-directory lookup fails,
             * continue with the current working directory.
             */
        }
        catch (...)
        {
            /*
             * Keep AI initialization non-fatal.
             */
        }
    }

    /*
     * Then try the current working directory.
     *
     * This also makes development launches from the
     * project root work when the model exists there.
     */

    try
    {
        const std::filesystem::path
            currentPath =
                std::filesystem::current_path();

        const std::filesystem::path
            candidate =
                currentPath /
                "models" /
                "detection" /
                fileName;

        if (
            std::filesystem::exists(
                candidate
            )
        )
        {
            return candidate;
        }
    }
    catch (...)
    {
        /*
         * Filesystem lookup failure is handled by the
         * caller as an unavailable AI model.
         */
    }

    /*
     * Return the normal application-relative path even
     * when it does not exist yet.
     *
     * This is useful for WebAssembly where the model can
     * later be packaged into the virtual filesystem.
     */

    return
        std::filesystem::path(
            "models"
        ) /
        "detection" /
        fileName;
}

}


IBVAPUI::IBVAPUI(
    SDL_Window* window,
    IBVAPVideoSourceManager& videoSourceManager
)
    : m_window(window),
      m_videoSourceManager(videoSourceManager)
{
}


IBVAPUI::~IBVAPUI()
{
    shutdown();
}


void IBVAPUI::initialize()
{
    if (m_initialized)
    {
        return;
    }

    setupStyle();

    m_database =
        std::make_unique<Database>();

    if (
        m_database->open()
    )
    {
        refreshHistory();
    }
    else
    {
        std::cerr
            << "IBVAP Database: initialization failed; History will be unavailable."
            << '\n';
    }

    /*
     * AI initialization is intentionally non-fatal.
     *
     * The surveillance UI and video sources must still
     * work if the model is unavailable.
     */
    initializeAI();

    m_initialized =
        true;
}


void IBVAPUI::setupStyle()
{
    ImGuiStyle& style =
        ImGui::GetStyle();

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding =
        ImVec2(8.0f, 8.0f);

    style.FramePadding =
        ImVec2(8.0f, 5.0f);

    style.ItemSpacing =
        ImVec2(8.0f, 6.0f);

    style.ItemInnerSpacing =
        ImVec2(6.0f, 4.0f);

    ImVec4* colors =
        style.Colors;

    colors[ImGuiCol_Text] =
        ImVec4(0.88f, 0.94f, 0.96f, 1.00f);

    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.42f, 0.52f, 0.55f, 1.00f);

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.025f, 0.045f, 0.055f, 1.00f);

    colors[ImGuiCol_ChildBg] =
        ImVec4(0.035f, 0.060f, 0.070f, 1.00f);

    colors[ImGuiCol_PopupBg] =
        ImVec4(0.035f, 0.060f, 0.070f, 0.98f);

    colors[ImGuiCol_Border] =
        ImVec4(0.10f, 0.30f, 0.34f, 1.00f);

    colors[ImGuiCol_BorderShadow] =
        ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg] =
        ImVec4(0.055f, 0.095f, 0.105f, 1.00f);

    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.07f, 0.16f, 0.18f, 1.00f);

    colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.08f, 0.20f, 0.22f, 1.00f);

    colors[ImGuiCol_TitleBg] =
        ImVec4(0.025f, 0.055f, 0.065f, 1.00f);

    colors[ImGuiCol_TitleBgActive] =
        ImVec4(0.025f, 0.055f, 0.065f, 1.00f);

    colors[ImGuiCol_MenuBarBg] =
        ImVec4(0.025f, 0.055f, 0.065f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] =
        ImVec4(0.02f, 0.035f, 0.04f, 1.00f);

    colors[ImGuiCol_ScrollbarGrab] =
        ImVec4(0.10f, 0.30f, 0.34f, 1.00f);

    colors[ImGuiCol_ScrollbarGrabHovered] =
        ImVec4(0.13f, 0.40f, 0.45f, 1.00f);

    colors[ImGuiCol_ScrollbarGrabActive] =
        ImVec4(0.15f, 0.48f, 0.54f, 1.00f);

    colors[ImGuiCol_CheckMark] =
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f);

    colors[ImGuiCol_SliderGrab] =
        ImVec4(0.15f, 0.65f, 0.70f, 1.00f);

    colors[ImGuiCol_SliderGrabActive] =
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f);

    colors[ImGuiCol_Button] =
        ImVec4(0.055f, 0.12f, 0.14f, 1.00f);

    colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.08f, 0.22f, 0.25f, 1.00f);

    colors[ImGuiCol_ButtonActive] =
        ImVec4(0.10f, 0.30f, 0.33f, 1.00f);

    colors[ImGuiCol_Header] =
        ImVec4(0.055f, 0.13f, 0.15f, 1.00f);

    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.08f, 0.22f, 0.25f, 1.00f);

    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.10f, 0.30f, 0.33f, 1.00f);

    colors[ImGuiCol_Separator] =
        ImVec4(0.10f, 0.30f, 0.34f, 1.00f);

    colors[ImGuiCol_SeparatorHovered] =
        ImVec4(0.15f, 0.55f, 0.60f, 1.00f);

    colors[ImGuiCol_SeparatorActive] =
        ImVec4(0.20f, 0.75f, 0.80f, 1.00f);
}


void IBVAPUI::render()
{
    if (!m_initialized)
    {
        return;
    }

    processPendingFileSelection();

    updateVideoTextures();

    cleanupUnusedTextures();

    ImGuiIO& io =
        ImGui::GetIO();

    ImGui::SetNextWindowPos(
        ImVec2(0.0f, 0.0f),
        ImGuiCond_Always
    );

    ImGui::SetNextWindowSize(
        io.DisplaySize,
        ImGuiCond_Always
    );

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    if (
        m_fullscreenSlot >= 0 &&
        static_cast<std::size_t>(
            m_fullscreenSlot
        ) < m_videoSlots.size()
    )
    {
        ImGui::Begin(
            "IBVAP_FULLSCREEN",
            nullptr,
            flags
        );

        renderVideoSlot(
            static_cast<std::size_t>(
                m_fullscreenSlot
            ),
            true
        );

        renderBottomBar();

        ImGui::End();
        renderAlertToasts();

        return;
    }

    ImGui::Begin(
        "IBVAP",
        nullptr,
        flags
    );

    renderTopBar();

    ImGui::Separator();

    renderWorkspace();

    renderBottomBar();

    ImGui::End();

    renderAddSourcePopup();
    renderAlertToasts();
}


void IBVAPUI::renderTopBar()
{
    ImGui::BeginChild(
        "TopBar",
        ImVec2(0.0f, 44.0f),
        false
    );

    ImGui::AlignTextToFramePadding();

    ImGui::TextColored(
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
        "IBVAP"
    );

    ImGui::SameLine();

    ImGui::TextDisabled(
        "STREAMING WORKSPACE"
    );

    const float historyButtonWidth =
        100.0f;

    const float addButtonWidth =
        130.0f;

    ImGui::SameLine(
        ImGui::GetWindowWidth() -
        historyButtonWidth -
        addButtonWidth -
        24.0f
    );

    if (
        ImGui::Button(
            m_showHistory ? "LIVE" : "HISTORY",
            ImVec2(
                historyButtonWidth,
                30.0f
            )
        )
    )
    {
        m_showHistory = !m_showHistory;

        if (m_showHistory)
        {
            refreshHistory();
        }
    }

    ImGui::SameLine();

    if (
        ImGui::Button(
            "+ ADD SOURCE",
            ImVec2(
                addButtonWidth,
                30.0f
            )
        )
    )
    {
        m_showAddSourcePopup =
            true;

        std::memset(
            m_sourceName,
            0,
            sizeof(m_sourceName)
        );

        std::memset(
            m_rtspUrl,
            0,
            sizeof(m_rtspUrl)
        );

        m_selectedSourceType =
            0;
    }

    ImGui::EndChild();
}


void IBVAPUI::renderWorkspace()
{
    const float panelWidth =
        330.0f;

    ImGui::BeginChild(
        "Workspace",
        ImVec2(
            0.0f,
            -42.0f
        ),
        false
    );

    if (m_showHistory)
    {
        renderHistory();
        ImGui::EndChild();
        return;
    }

    renderLeftPanel();

    ImGui::SameLine();

    ImGui::BeginChild(
        "MainVideoWorkspace",
        ImVec2(
            -panelWidth - 8.0f,
            0.0f
        ),
        true
    );

    renderVideoArea();

    ImGui::EndChild();

    ImGui::SameLine();

    renderRightPanel();

    ImGui::EndChild();
}


void IBVAPUI::renderLeftPanel()
{
    const float panelWidth =
        170.0f;

    ImGui::BeginChild(
        "LeftPanel",
        ImVec2(
            panelWidth,
            0.0f
        ),
        true
    );

    ImGui::TextColored(
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
        "SOURCES"
    );

    ImGui::Separator();

    if (
        m_videoSlots.empty()
    )
    {
        ImGui::Spacing();

        ImGui::TextWrapped(
            "No video sources."
        );

        ImGui::TextDisabled(
            "Use + ADD SOURCE to begin."
        );
    }

    for (
        std::size_t i = 0;
        i < m_videoSlots.size();
        ++i
    )
    {
        const VideoSlot& slot =
            m_videoSlots[i];

        const bool selected =
            m_selectedSlot ==
            static_cast<int>(i);

        std::string label =
            std::to_string(i + 1) +
            ". " +
            slot.name;

        if (
            label.empty()
        )
        {
            label =
                std::to_string(i + 1) +
                ". Source";
        }

        if (
            ImGui::Selectable(
                label.c_str(),
                selected,
                ImGuiSelectableFlags_AllowDoubleClick
            )
        )
        {
            m_selectedSlot =
                static_cast<int>(i);
        }
    }

    ImGui::EndChild();
}


void IBVAPUI::renderVideoArea()
{
    ImGui::BeginChild(
        "VideoArea",
        ImVec2(0.0f, 0.0f),
        false
    );

    if (
        m_videoSlots.empty()
    )
    {
        const ImVec2 available =
            ImGui::GetContentRegionAvail();

        const char* message =
            "NO VIDEO SOURCES";

        const ImVec2 textSize =
            ImGui::CalcTextSize(
                message
            );

        ImGui::SetCursorPos(
            ImVec2(
                std::max(
                    0.0f,
                    (
                        available.x -
                        textSize.x
                    ) * 0.5f
                ),
                std::max(
                    0.0f,
                    (
                        available.y -
                        textSize.y
                    ) * 0.5f
                )
            )
        );

        ImGui::TextDisabled(
            "%s",
            message
        );

        ImGui::EndChild();

        return;
    }

    std::size_t columns =
        1;

    if (
        m_videoSlots.size() >= 2
    )
    {
        columns = 2;
    }

    if (
        m_videoSlots.size() >= 5
    )
    {
        columns = 3;
    }

    if (
        m_videoSlots.size() >= 10
    )
    {
        columns = 4;
    }

    const float spacing =
        ImGui::GetStyle().ItemSpacing.x;

    const float availableWidth =
        ImGui::GetContentRegionAvail().x;

    const float slotWidth =
        (
            availableWidth -
            spacing *
            static_cast<float>(
                columns - 1
            )
        ) /
        static_cast<float>(
            columns
        );

    for (
        std::size_t i = 0;
        i < m_videoSlots.size();
        ++i
    )
    {
        if (
            i > 0 &&
            i % columns != 0
        )
        {
            ImGui::SameLine();
        }

        ImGui::BeginChild(
            (
                "VideoSlot_" +
                std::to_string(i)
            ).c_str(),
            ImVec2(
                slotWidth,
                0.0f
            ),
            true
        );

        renderVideoSlot(
            i,
            false
        );

        ImGui::EndChild();
    }

    ImGui::EndChild();
}


// ==================== END PART 1 / 6 ====================
// ==================== PART 2 / 6 ====================

void IBVAPUI::renderVideoSlot(
    std::size_t slotIndex,
    bool fullscreen
)
{
    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return;
    }

    VideoSlot& slot =
        m_videoSlots[slotIndex];

    const VideoSourceInfo* info =
        nullptr;

    if (
        !slot.sourceId.empty()
    )
    {
        info =
            m_videoSourceManager.getSourceInfo(
                slot.sourceId
            );
    }

    VideoSourceState state =
        VideoSourceState::Created;

    if (
        !slot.sourceId.empty()
    )
    {
        state =
            m_videoSourceManager.getSourceState(
                slot.sourceId
            );
    }

    ImGui::TextColored(
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
        "%s",
        slot.type.c_str()
    );

    if (info)
    {
        ImGui::SameLine();

        if (
            state ==
            VideoSourceState::Running
        )
        {
            ImGui::TextColored(
                ImVec4(0.25f, 0.85f, 0.65f, 1.00f),
                "LIVE"
            );
        }
        else if (
            state ==
            VideoSourceState::Starting
        )
        {
            ImGui::TextColored(
                ImVec4(0.20f, 0.75f, 0.85f, 1.00f),
                "STARTING"
            );
        }
        else if (
            state ==
            VideoSourceState::Ready
        )
        {
            ImGui::TextColored(
                ImVec4(0.30f, 0.75f, 0.85f, 1.00f),
                "READY"
            );
        }
        else if (
            state ==
            VideoSourceState::WaitingForDevice
        )
        {
            ImGui::TextDisabled(
                "WAITING"
            );
        }
        else if (
            state ==
            VideoSourceState::Error
        )
        {
            ImGui::TextDisabled(
                "ERROR"
            );
        }
    }

    ImGui::Separator();

    ImGui::PushID(static_cast<int>(slotIndex));
    if (ImGui::Button(
            slot.fencePlacementMode ? "CANCEL FENCE PLACEMENT" : "PLACE VIRTUAL FENCE"
        ))
    {
        slot.fencePlacementMode = !slot.fencePlacementMode;
        slot.fenceDrawing = false;
    }
    if (slot.hasVirtualFence)
    {
        ImGui::SameLine();
        if (ImGui::Button("CLEAR FENCE"))
        {
            slot.hasVirtualFence = false;
            slot.fencePlacementMode = false;
            slot.fenceDrawing = false;
            slot.previousFenceDetections.clear();
        }
    }
    if (slot.fencePlacementMode)
    {
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(1.0f, 0.65f, 0.20f, 1.0f),
            "Drag a rectangle over the video"
        );
    }
    ImGui::PopID();

    const bool isMobileSlot =
        info &&
        info->type ==
            VideoSourceType::MobileStream;

    const ImVec2 available =
        ImGui::GetContentRegionAvail();

    const float controlReserve =
        fullscreen
            ? 70.0f
            : 145.0f;

    const float videoHeight =
        std::max(
            160.0f,
            available.y -
            controlReserve
        );

    float networkPanelWidth =
        0.0f;

    float videoWidth =
        std::max(
            100.0f,
            available.x
        );

    if (
        isMobileSlot &&
        !fullscreen
    )
    {
        networkPanelWidth =
            std::min(
                135.0f,
                std::max(
                    95.0f,
                    available.x * 0.34f
                )
            );

        const float telemetrySpacing =
            ImGui::GetStyle().ItemSpacing.x;

        videoWidth =
            std::max(
                100.0f,
                available.x -
                networkPanelWidth -
                telemetrySpacing
            );

        if (
            videoWidth < 100.0f
        )
        {
            networkPanelWidth =
                std::max(
                    80.0f,
                    available.x -
                    telemetrySpacing -
                    100.0f
                );

            videoWidth =
                std::max(
                    80.0f,
                    available.x -
                    networkPanelWidth -
                    telemetrySpacing
                );
        }
    }

    auto renderVideoContent =
        [&]()
        {
            auto textureIt =
                m_videoTextures.find(
                    slot.sourceId
                );

            bool displayedTexture =
                false;

            if (
                textureIt !=
                    m_videoTextures.end() &&
                textureIt->second.texture != 0 &&
                textureIt->second.width > 0 &&
                textureIt->second.height > 0
            )
            {
                VideoTexture& texture =
                    textureIt->second;

                const float sourceAspect =
                    static_cast<float>(
                        texture.width
                    ) /
                    static_cast<float>(
                        texture.height
                    );

                const float displayAspect =
                    videoWidth /
                    videoHeight;

                float u0 = 0.0f;
                float u1 = 1.0f;
                float v0 = 0.0f;
                float v1 = 1.0f;

                if (
                    sourceAspect >
                    displayAspect
                )
                {
                    const float visibleWidth =
                        displayAspect /
                        sourceAspect;

                    const float crop =
                        (
                            1.0f -
                            visibleWidth
                        ) * 0.5f;

                    u0 = crop;
                    u1 = 1.0f - crop;
                }
                else if (
                    sourceAspect <
                    displayAspect
                )
                {
                    const float visibleHeight =
                        sourceAspect /
                        displayAspect;

                    const float crop =
                        (
                            1.0f -
                            visibleHeight
                        ) * 0.5f;

                    v0 = crop;
                    v1 = 1.0f - crop;
                }

                ImGui::Image(
                    static_cast<ImTextureID>(
                        texture.texture
                    ),
                    ImVec2(
                        videoWidth,
                        videoHeight
                    ),
                    ImVec2(
                        u0,
                        v0
                    ),
                    ImVec2(
                        u1,
                        v1
                    )
                );

                /*
                 * The detection overlay is drawn immediately
                 * after the video image so GetItemRectMin()
                 * and GetItemRectMax() refer to this exact
                 * video rectangle.
                 */
                renderAIOverlay(
                    slot
                );

                displayedTexture =
                    true;
            }

            if (
                !displayedTexture
            )
            {
                ImGui::BeginChild(
                    (
                        "Placeholder_" +
                        slot.sourceId +
                        "_" +
                        std::to_string(
                            slotIndex
                        )
                    ).c_str(),
                    ImVec2(
                        videoWidth,
                        videoHeight
                    ),
                    true
                );

                if (
                    isMobileSlot &&
                    slot.qrReady &&
                    !slot.mobileUrl.empty()
                )
                {
                    ImGui::TextColored(
                        ImVec4(
                            0.20f,
                            0.85f,
                            0.90f,
                            1.00f
                        ),
                        "SCAN TO CONNECT MOBILE CAMERA"
                    );

                    ImGui::Spacing();

                    renderMobileQrCode(
                        slot,
                        std::min(
                            videoWidth * 0.62f,
                            videoHeight * 0.58f
                        )
                    );

                    ImGui::Spacing();

                    const char* text =
                        slot.videoError
                            ? "MOBILE STREAM ERROR"
                            : (
                                state ==
                                    VideoSourceState::Running
                                    ? "RECEIVING MOBILE VIDEO"
                                    : "WAITING FOR MOBILE CAMERA"
                            );

                    const ImVec2 textSize =
                        ImGui::CalcTextSize(
                            text
                        );

                    ImGui::SetCursorPosX(
                        std::max(
                            0.0f,
                            (
                                videoWidth -
                                textSize.x
                            ) * 0.5f
                        )
                    );

                    ImGui::TextDisabled(
                        "%s",
                        text
                    );
                }
                else
                {
                    const char* text =
                        nullptr;

                    if (isMobileSlot)
                    {
                        text =
                            slot.videoError
                                ? "MOBILE STREAM ERROR"
                                : (
                                    state ==
                                        VideoSourceState::Running
                                        ? "RECEIVING MOBILE VIDEO"
                                        : "WAITING FOR MOBILE CAMERA"
                                );
                    }
                    else
                    {
                        text =
                            slot.videoError
                                ? "VIDEO ERROR"
                                : (
                                    slot.hasVideo
                                        ? "WAITING FOR FRAME"
                                        : "NO VIDEO"
                                );
                    }

                    const ImVec2 textSize =
                        ImGui::CalcTextSize(
                            text
                        );

                    ImGui::SetCursorPos(
                        ImVec2(
                            std::max(
                                0.0f,
                                (
                                    videoWidth -
                                    textSize.x
                                ) * 0.5f
                            ),
                            std::max(
                                0.0f,
                                (
                                    videoHeight -
                                    textSize.y
                                ) * 0.5f
                            )
                        )
                    );

                    ImGui::TextDisabled(
                        "%s",
                        text
                    );
                }

                ImGui::EndChild();
            }
        };

    if (
        isMobileSlot &&
        !fullscreen
    )
    {
        ImGui::BeginChild(
            (
                "MobileVideo_" +
                std::to_string(
                    slotIndex
                )
            ).c_str(),
            ImVec2(
                videoWidth,
                videoHeight
            ),
            false
        );

        renderVideoContent();

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild(
            (
                "MobileNetwork_" +
                std::to_string(
                    slotIndex
                )
            ).c_str(),
            ImVec2(
                networkPanelWidth,
                videoHeight
            ),
            true
        );

        renderMobileNetworkPanel(
            slot
        );

        ImGui::EndChild();
    }
    else
    {
        renderVideoContent();
    }

    ImGui::Spacing();

    if (isMobileSlot)
    {
        if (
            state !=
            VideoSourceState::Running
        )
        {
            if (
                ImGui::Button(
                    "START STREAMING"
                )
            )
            {
                if (
                    m_videoSourceManager.startSource(
                        slot.sourceId
                    )
                )
                {
                    slot.videoError =
                        false;
                }
                else
                {
                    slot.videoError =
                        true;
                }
            }
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.25f, 0.85f, 0.65f, 1.00f),
                "STREAMING"
            );
        }

        ImGui::Spacing();

        if (
            !slot.mobileUrl.empty()
        )
        {
            ImGui::TextDisabled(
                "Mobile URL"
            );

            ImGui::PushItemWidth(
                std::max(
                    100.0f,
                    videoWidth - 8.0f
                )
            );

            char urlBuffer[2048] =
                {};

            std::snprintf(
                urlBuffer,
                sizeof(urlBuffer),
                "%s",
                slot.mobileUrl.c_str()
            );

            ImGui::InputText(
                "##MobileURL",
                urlBuffer,
                sizeof(urlBuffer),
                ImGuiInputTextFlags_ReadOnly
            );

            ImGui::PopItemWidth();

            if (
                ImGui::Button(
                    "COPY URL"
                )
            )
            {
                copyMobileUrl(
                    slot.mobileUrl
                );
            }

            ImGui::SameLine();

            ImGui::TextDisabled(
                "Scan the QR code or paste this URL on the mobile device."
            );
        }
    }

    const bool isPreRecordedSlot =
        (
            info &&
            info->type ==
                VideoSourceType::LocalVideo
        ) ||
        (
            info == nullptr &&
            slot.type ==
                "Pre-recorded video"
        );

    if (
        isPreRecordedSlot
    )
    {
        if (
            ImGui::Button(
                "SELECT VIDEO FROM DEVICE"
            )
        )
        {
            requestVideoFile(
                slotIndex
            );
        }

        ImGui::SameLine();

        const bool canReplay =
            !slot.sourceId.empty() &&
            !slot.filePath.empty();

        if (!canReplay)
        {
            ImGui::BeginDisabled();
        }

        if (
            ImGui::Button(
                "REPLAY"
            )
        )
        {
            if (
                !slot.sourceId.empty()
            )
            {
                m_videoSourceManager.replayLocalVideo(
                    slot.sourceId
                );
            }
        }

        if (!canReplay)
        {
            ImGui::EndDisabled();
        }
    }

    const char* fullscreenLabel =
        (
            m_fullscreenSlot ==
            static_cast<int>(
                slotIndex
            )
        )
            ? "EXIT FULL SCREEN"
            : "FULL SCREEN";

    ImGui::SameLine();

    if (
        ImGui::Button(
            fullscreenLabel
        )
    )
    {
        if (
            m_fullscreenSlot ==
            static_cast<int>(
                slotIndex
            )
        )
        {
            m_fullscreenSlot =
                -1;
        }
        else
        {
            m_fullscreenSlot =
                static_cast<int>(
                    slotIndex
                );
        }
    }

    ImGui::SameLine();

    if (
        ImGui::Button(
            "REMOVE"
        )
    )
    {
        removeVideoSlot(
            slotIndex
        );

        return;
    }

    ImGui::Spacing();

    if (info)
    {
        const char* displayName =
            info->name.empty()
                ? slot.name.c_str()
                : info->name.c_str();

        ImGui::Text(
            "Device: %s",
            displayName
        );

        if (
            !info->address.empty()
        )
        {
            ImGui::TextDisabled(
                "Address: %s",
                info->address.c_str()
            );
        }
        else
        {
            ImGui::TextDisabled(
                "Address: not assigned"
            );
        }

        if (
            info->width > 0 &&
            info->height > 0
        )
        {
            ImGui::TextDisabled(
                "%ux%u",
                info->width,
                info->height
            );
        }

        if (
            info->frameRate > 0.0
        )
        {
            ImGui::SameLine();

            ImGui::TextDisabled(
                "%.1f FPS",
                info->frameRate
            );
        }
    }
    else
    {
        ImGui::Text(
            "Device: %s",
            slot.name.c_str()
        );

        ImGui::TextDisabled(
            "Address: not available"
        );
    }
}


void IBVAPUI::renderMobileNetworkPanel(
    const VideoSlot& slot
)
{
#ifdef __EMSCRIPTEN__

    (void)slot;

    ImGui::TextDisabled(
        "NETWORK"
    );

    ImGui::Separator();

    ImGui::TextDisabled(
        "Not available"
    );

    return;

#else

    if (
        slot.sourceId.empty()
    )
    {
        ImGui::TextDisabled(
            "NETWORK"
        );

        ImGui::Separator();

        ImGui::TextDisabled(
            "No source"
        );

        return;
    }

    const MobileNetworkStats stats =
        m_videoSourceManager.getMobileNetworkStats(
            slot.sourceId
        );

    ImGui::TextColored(
        ImVec4(
            0.20f,
            0.85f,
            0.90f,
            1.00f
        ),
        "NETWORK"
    );

    ImGui::Separator();

    ImGui::TextDisabled(
        "CONNECTION"
    );

    if (
        stats.connected
    )
    {
        ImGui::TextColored(
            ImVec4(
                0.25f,
                0.85f,
                0.65f,
                1.00f
            ),
            "CONNECTED"
        );
    }
    else
    {
        ImGui::TextColored(
            ImVec4(
                0.75f,
                0.45f,
                0.30f,
                1.00f
            ),
            "OFFLINE"
        );
    }

    ImGui::Spacing();

    ImGui::TextDisabled(
        "RTT"
    );

    if (
        stats.probesReceived > 0
    )
    {
        ImGui::Text(
            "%.1f ms",
            stats.rttMs
        );
    }
    else
    {
        ImGui::TextDisabled(
            "--"
        );
    }

    ImGui::Spacing();

    ImGui::TextDisabled(
        "JITTER"
    );

    if (
        stats.probesReceived > 1
    )
    {
        ImGui::Text(
            "%.1f ms",
            stats.jitterMs
        );
    }
    else
    {
        ImGui::TextDisabled(
            "--"
        );
    }

    ImGui::Spacing();

    ImGui::TextDisabled(
        "LOSS"
    );

    const std::uint64_t totalProbes =
        stats.probesReceived +
        stats.probesLost;

    if (
        totalProbes > 0
    )
    {
        ImGui::Text(
            "%.1f%%",
            stats.lossPercent
        );
    }
    else
    {
        ImGui::TextDisabled(
            "--"
        );
    }

    ImGui::Spacing();

    ImGui::Separator();

    if (
        stats.measuring
    )
    {
        ImGui::TextDisabled(
            "Measuring..."
        );
    }
    else if (
        stats.connected
    )
    {
        ImGui::TextDisabled(
            "Waiting for probe"
        );
    }
    else
    {
        ImGui::TextDisabled(
            "Waiting for phone"
        );
    }

#endif
}


void IBVAPUI::renderVideoPlaceholder(
    const VideoSlot& slot
)
{
    const ImVec2 available =
        ImGui::GetContentRegionAvail();

    const float width =
        std::max(
            100.0f,
            available.x
        );

    const float height =
        std::max(
            140.0f,
            std::min(
                available.y,
                300.0f
            )
        );

    ImGui::BeginChild(
        (
            "Placeholder_Legacy_" +
            slot.sourceId
        ).c_str(),
        ImVec2(
            width,
            height
        ),
        true
    );

    const char* text =
        slot.videoError
            ? "VIDEO ERROR"
            : (
                slot.hasVideo
                    ? "WAITING FOR FRAME"
                    : "NO VIDEO"
            );

    const ImVec2 textSize =
        ImGui::CalcTextSize(
            text
        );

    ImGui::SetCursorPos(
        ImVec2(
            std::max(
                0.0f,
                (
                    width -
                    textSize.x
                ) * 0.5f
            ),
            std::max(
                0.0f,
                (
                    height -
                    textSize.y
                ) * 0.5f
            )
        )
    );

    ImGui::TextDisabled(
        "%s",
        text
    );

    ImGui::EndChild();
}


// ==================== END PART 2 / 6 ====================
// ==================== PART 3 / 6 ====================

void IBVAPUI::renderRightPanel()
{
    const float panelWidth =
        330.0f;

    ImGui::BeginChild(
        "RightPanel",
        ImVec2(
            panelWidth,
            0.0f
        ),
        true
    );

    renderDashboard();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
        "SOURCE INFO"
    );

    ImGui::Separator();

    if (
        m_selectedSlot < 0 ||
        static_cast<std::size_t>(
            m_selectedSlot
        ) >= m_videoSlots.size()
    )
    {
        ImGui::TextDisabled(
            "Select a source."
        );

        ImGui::EndChild();

        return;
    }

    const VideoSlot& slot =
        m_videoSlots[
            static_cast<std::size_t>(
                m_selectedSlot
            )
        ];

    const VideoSourceInfo* info =
        nullptr;

    if (
        !slot.sourceId.empty()
    )
    {
        info =
            m_videoSourceManager.getSourceInfo(
                slot.sourceId
            );
    }

    if (
        !info &&
        slot.type ==
            "Pre-recorded video"
    )
    {
        ImGui::Text(
            "Name"
        );

        ImGui::TextWrapped(
            "%s",
            slot.name.c_str()
        );

        ImGui::Spacing();

        ImGui::Text(
            "Type"
        );

        ImGui::TextDisabled(
            "Pre-recorded video"
        );

        ImGui::Spacing();

        ImGui::Text(
            "State"
        );

        ImGui::TextDisabled(
            "Waiting for video"
        );

        ImGui::Spacing();

        ImGui::Text(
            "File"
        );

        ImGui::TextDisabled(
            "No file selected"
        );

        ImGui::EndChild();

        return;
    }

    if (!info)
    {
        ImGui::TextDisabled(
            "Source unavailable."
        );

        ImGui::EndChild();

        return;
    }

    ImGui::Text(
        "Name"
    );

    ImGui::TextWrapped(
        "%s",
        info->name.c_str()
    );

    ImGui::Spacing();

    ImGui::Text(
        "Type"
    );

    ImGui::TextDisabled(
        "%s",
        slot.type.c_str()
    );

    ImGui::Spacing();

    ImGui::Text(
        "State"
    );

    const char* stateText =
        "Unknown";

    switch (
        m_videoSourceManager.getSourceState(
            slot.sourceId
        )
    )
    {
        case VideoSourceState::Created:
            stateText = "Created";
            break;

        case VideoSourceState::Starting:
            stateText = "Starting";
            break;

        case VideoSourceState::Running:
            stateText = "Live";
            break;

        case VideoSourceState::Paused:
            stateText = "Paused";
            break;

        case VideoSourceState::Stopping:
            stateText = "Stopping";
            break;

        case VideoSourceState::Stopped:
            stateText = "Stopped";
            break;

        case VideoSourceState::WaitingForDevice:
            stateText = "Waiting for mobile";
            break;

        case VideoSourceState::Ready:
            stateText = "Ready";
            break;

        case VideoSourceState::Error:
            stateText = "Error";
            break;
    }

    ImGui::TextDisabled(
        "%s",
        stateText
    );

    ImGui::Spacing();

    /*
     * AI status is intentionally kept compact in the
     * existing source-information panel.
     */

    ImGui::Text(
        "AI Detection"
    );

    if (
        m_aiInitialized &&
        slot.aiAvailable
    )
    {
        ImGui::TextColored(
            ImVec4(
                0.25f,
                0.85f,
                0.65f,
                1.00f
            ),
            "ACTIVE"
        );

        ImGui::TextDisabled(
            "Objects: %zu",
            slot.aiResult.detections.size()
        );
    }
    else if (
        m_aiInitialized
    )
    {
        ImGui::TextDisabled(
            "Waiting for frame"
        );
    }
    else
    {
        ImGui::TextDisabled(
            "Unavailable"
        );
    }

    ImGui::Spacing();

    if (
        !info->address.empty()
    )
    {
        ImGui::Text(
            "Address"
        );

        ImGui::TextWrapped(
            "%s",
            info->address.c_str()
        );

        ImGui::Spacing();
    }

    if (
        info->type ==
        VideoSourceType::MobileStream
    )
    {
        ImGui::Text(
            "Mobile URL"
        );

        if (
            !slot.mobileUrl.empty()
        )
        {
            ImGui::TextWrapped(
                "%s",
                slot.mobileUrl.c_str()
            );

            if (
                ImGui::Button(
                    "COPY URL##RightPanel"
                )
            )
            {
                copyMobileUrl(
                    slot.mobileUrl
                );
            }

            if (
                slot.qrReady
            )
            {
                ImGui::Spacing();

                ImGui::TextDisabled(
                    "SCAN MOBILE CAMERA"
                );

                renderMobileQrCode(
                    slot,
                    std::min(
                        ImGui::GetContentRegionAvail().x,
                        130.0f
                    )
                );
            }
        }
        else
        {
            ImGui::TextDisabled(
                "Not available"
            );
        }
    }
    else if (
        info->type ==
        VideoSourceType::LaptopCamera
    )
    {
        ImGui::Text(
            "Resolution"
        );

        if (
            info->width > 0 &&
            info->height > 0
        )
        {
            ImGui::TextDisabled(
                "%u x %u",
                info->width,
                info->height
            );
        }
        else
        {
            ImGui::TextDisabled(
                "Not available"
            );
        }

        ImGui::Spacing();

        ImGui::Text(
            "Frame rate"
        );

        if (
            info->frameRate > 0.0
        )
        {
            ImGui::TextDisabled(
                "%.1f FPS",
                info->frameRate
            );
        }
        else
        {
            ImGui::TextDisabled(
                "Not available"
            );
        }
    }
    else if (
        info->type ==
        VideoSourceType::LocalVideo
    )
    {
        ImGui::Text(
            "File"
        );

        if (
            !slot.filePath.empty()
        )
        {
            ImGui::TextWrapped(
                "%s",
                slot.filePath.c_str()
            );
        }
        else
        {
            ImGui::TextDisabled(
                "No file selected"
            );
        }
    }

    ImGui::EndChild();
}


void IBVAPUI::renderBottomBar()
{
    ImGui::BeginChild(
        "BottomBar",
        ImVec2(
            0.0f,
            34.0f
        ),
        false
    );

    const char* credit = "Developed by Coding Leyaks";
    const float textWidth = ImGui::CalcTextSize(credit).x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(
        std::max(0.0f, (availableWidth - textWidth) * 0.5f)
    );
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", credit);

    ImGui::EndChild();
}


void IBVAPUI::queueAlert(
    const std::string& message,
    const std::string& sourceId,
    std::uint32_t color
)
{
    const auto now = std::chrono::steady_clock::now();
    const std::string key = sourceId + "\n" + message;
    const auto previous = m_alertCooldowns.find(key);
    if (
        previous != m_alertCooldowns.end() &&
        now - previous->second < std::chrono::seconds(6)
    )
    {
        return;
    }

    m_alertCooldowns[key] = now;
    m_alertToasts.push_back({message, color, now});
    if (m_alertToasts.size() > 4)
        m_alertToasts.erase(m_alertToasts.begin());
}


void IBVAPUI::renderAlertToasts()
{
    const auto now = std::chrono::steady_clock::now();
    m_alertToasts.erase(
        std::remove_if(
            m_alertToasts.begin(),
            m_alertToasts.end(),
            [now](const AlertToast& alert)
            {
                return now - alert.createdAt > std::chrono::seconds(5);
            }
        ),
        m_alertToasts.end()
    );

    for (auto it = m_alertCooldowns.begin(); it != m_alertCooldowns.end();)
    {
        if (now - it->second > std::chrono::seconds(30))
            it = m_alertCooldowns.erase(it);
        else
            ++it;
    }

    if (m_alertToasts.empty())
        return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float toastWidth = std::min(360.0f, std::max(220.0f, display.x - 24.0f));
    float y = 12.0f;

    for (auto it = m_alertToasts.rbegin(); it != m_alertToasts.rend(); ++it)
    {
        const ImVec2 textSize = ImGui::CalcTextSize(it->message.c_str());
        const float height = std::max(38.0f, textSize.y + 18.0f);
        const float x = std::max(12.0f, display.x - toastWidth - 12.0f);
        const ImVec2 topLeft(x, y);
        const ImVec2 bottomRight(x + toastWidth, y + height);

        drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(8, 20, 25, 238), 5.0f);
        drawList->AddRect(topLeft, bottomRight, it->color, 5.0f, 0, 2.0f);
        drawList->AddRectFilled(
            topLeft,
            ImVec2(x + 5.0f, bottomRight.y),
            it->color,
            3.0f
        );
        drawList->AddText(
            ImVec2(x + 16.0f, y + (height - textSize.y) * 0.5f),
            IM_COL32(245, 250, 250, 255),
            it->message.c_str()
        );
        y += height + 8.0f;
    }
}


void IBVAPUI::renderAddSourcePopup()
{
    if (
        m_showAddSourcePopup
    )
    {
        ImGui::OpenPopup(
            "Add Video Source"
        );

        m_showAddSourcePopup =
            false;
    }

    if (
        !ImGui::BeginPopupModal(
            "Add Video Source",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        return;
    }

    ImGui::TextColored(
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
        "ADD VIDEO SOURCE"
    );

    ImGui::Separator();

    ImGui::Spacing();

    ImGui::Text(
        "Source Name"
    );

    ImGui::SetNextItemWidth(
        380.0f
    );

    ImGui::InputText(
        "##SourceName",
        m_sourceName,
        sizeof(m_sourceName)
    );

    ImGui::Spacing();

    ImGui::Text(
        "Source Type"
    );

    const char* sourceTypes[] =
    {
        "RTSP Stream",
        "Laptop Camera",
        "Pre-recorded video",
        "Mobile Camera"
    };

    ImGui::SetNextItemWidth(
        380.0f
    );

    ImGui::Combo(
        "##SourceType",
        &m_selectedSourceType,
        sourceTypes,
        IM_ARRAYSIZE(
            sourceTypes
        )
    );

    ImGui::Spacing();

    if (
        m_selectedSourceType == 0
    )
    {
        ImGui::Separator();

        ImGui::Spacing();

        ImGui::Text(
            "RTSP URL"
        );

        ImGui::SetNextItemWidth(
            380.0f
        );

        ImGui::InputText(
            "##RTSPUrl",
            m_rtspUrl,
            sizeof(m_rtspUrl)
        );

        ImGui::TextDisabled(
            "Example: rtsp://192.168.1.10:8554/live"
        );

        ImGui::Spacing();
    }

    if (
        m_selectedSourceType == 1
    )
    {
        ImGui::Separator();

        ImGui::Spacing();

        ImGui::TextDisabled(
            "Uses the main laptop camera."
        );

        ImGui::Spacing();
    }

    if (
        m_selectedSourceType == 2
    )
    {
        ImGui::Separator();

        ImGui::Spacing();

        ImGui::TextDisabled(
            "Create the slot first."
        );

        ImGui::TextDisabled(
            "Select the video from the slot controls."
        );

        ImGui::Spacing();
    }

    if (
        m_selectedSourceType == 3
    )
    {
        ImGui::Separator();

        ImGui::Spacing();

        ImGui::TextDisabled(
            "Uses a mobile phone camera."
        );

        ImGui::TextDisabled(
            "A QR code will be generated for this mobile slot."
        );

        ImGui::TextDisabled(
            "Scan it with the phone camera to open the correct URL."
        );

        ImGui::TextDisabled(
            "After camera permission, streaming starts automatically."
        );

        ImGui::Spacing();
    }

    ImGui::Separator();

    ImGui::Spacing();

    const float buttonWidth =
        150.0f;

    if (
        ImGui::Button(
            "CREATE SLOT",
            ImVec2(
                buttonWidth,
                32.0f
            )
        )
    )
    {
        std::string sourceName =
            m_sourceName;

        if (
            sourceName.empty()
        )
        {
            switch (
                m_selectedSourceType
            )
            {
                case 0:
                    sourceName =
                        "RTSP Stream";
                    break;

                case 1:
                    sourceName =
                        "Laptop Camera";
                    break;

                case 2:
                    sourceName =
                        "Local Video";
                    break;

                case 3:
                    sourceName =
                        "Mobile Camera";
                    break;

                default:
                    sourceName =
                        "Video Source";
                    break;
            }
        }

        VideoSlot slot;

        slot.name =
            sourceName;

        slot.type =
            sourceTypes[
                m_selectedSourceType
            ];

        slot.sourceId.clear();
        slot.filePath.clear();
        slot.mobileUrl.clear();

        slot.hasVideo =
            false;

        slot.videoError =
            false;

        slot.qrModules.clear();

        slot.qrVersion =
            0;

        slot.qrSize =
            0;

        slot.qrReady =
            false;

        slot.aiResult.clear();

        slot.aiAvailable =
            false;

        slot.aiError =
            false;

        slot.aiFrameCounter =
            0;

        /*
         * Synchronization state.
         *
         * No frame has been displayed or analyzed yet.
         */
        slot.aiResultGeneration =
            0;

        slot.displayedFrameSequence =
            0;

        slot.aiResultFrameSequence =
            0;

        slot.aiLastTimestampUs =
            0;

        slot.dashboardDetections.clear();

        m_videoSlots.push_back(
            std::move(slot)
        );

        const std::size_t slotIndex =
            m_videoSlots.size() - 1;

        bool success =
            true;

        switch (
            m_selectedSourceType
        )
        {
            case 0:
                success =
                    createRTSPSource(
                        slotIndex
                    );
                break;

            case 1:
                success =
                    createCameraSource(
                        slotIndex
                    );
                break;

            case 2:
                success =
                    true;
                break;

            case 3:
                success =
                    createMobileSource(
                        slotIndex
                    );
                break;

            default:
                success =
                    false;
                break;
        }

        if (
            !success
        )
        {
            if (
                slotIndex <
                m_videoSlots.size()
            )
            {
                removeVideoSlot(
                    slotIndex
                );
            }
        }
        else
        {
            m_selectedSlot =
                static_cast<int>(
                    slotIndex
                );
        }

        std::memset(
            m_sourceName,
            0,
            sizeof(m_sourceName)
        );

        std::memset(
            m_rtspUrl,
            0,
            sizeof(m_rtspUrl)
        );

        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (
        ImGui::Button(
            "CANCEL",
            ImVec2(
                buttonWidth,
                32.0f
            )
        )
    )
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}


// ==================== END PART 3 / 6 ====================
// ==================== PART 4 / 6 ====================

void IBVAPUI::updateVideoTextures()
{
    for (
        VideoSlot& slot :
        m_videoSlots
    )
    {
        if (
            slot.sourceId.empty()
        )
        {
            continue;
        }

        Frame frame;

        const bool received =
            m_videoSourceManager.tryGetFrame(
                slot.sourceId,
                frame
            );

        if (
            !received ||
            !frame.valid()
        )
        {
            continue;
        }

        static bool printedMobileFrame =
            false;

        if (
            !printedMobileFrame
        )
        {
            const VideoSourceInfo* info =
                m_videoSourceManager.getSourceInfo(
                    slot.sourceId
                );

            if (
                info &&
                info->type ==
                    VideoSourceType::MobileStream
            )
            {
                std::cout
                    << "IBVAP UI: received mobile Frame | "
                       "source="
                    << slot.sourceId
                    << " | resolution="
                    << frame.width
                    << "x"
                    << frame.height
                    << " | stride="
                    << frame.stride
                    << " | bytes="
                    << frame.byteSize()
                    << " | sequence="
                    << frame.sequence
                    << '\n';

                printedMobileFrame =
                    true;
            }
        }

        /*
         * AI and video receive the exact same Frame.
         *
         * Native:
         *
         *      Frame
         *        |
         *        +----> OpenGL texture
         *        |
         *        +----> AI worker
         *
         * The AI worker never blocks video presentation.
         *
         * WASM:
         *
         *      Frame
         *        |
         *        +----> WebGL texture
         *        |
         *        +----> synchronous AI
         *
         * The existing WASM architecture remains unchanged.
         */
        runAIForSlot(
            slot,
            frame
        );

        /*
         * The displayed sequence is updated ONLY after the
         * texture upload succeeds.
         *
         * Therefore displayedFrameSequence always identifies
         * the actual frame currently contained in the texture.
         */
        if (
            uploadFrameToTexture(
                slot.sourceId,
                frame
            )
        )
        {
            slot.hasVideo =
                true;

            slot.videoError =
                false;

            slot.displayedFrameSequence =
                frame.sequence;
        }
        else
        {
            slot.videoError =
                true;
        }
    }
}


bool IBVAPUI::uploadFrameToTexture(
    const std::string& sourceId,
    const Frame& frame
)
{
    if (
        !frame.valid()
    )
    {
        return false;
    }

    GLenum sourceFormat =
        GL_RGBA;

    GLenum internalFormat =
        GL_RGBA8;

    std::size_t bytesPerPixel =
        0;

    switch (
        frame.format
    )
    {
        case PixelFormat::RGB8:
            sourceFormat =
                GL_RGB;

            internalFormat =
                GL_RGB8;

            bytesPerPixel =
                3;

            break;

        case PixelFormat::RGBA8:
            sourceFormat =
                GL_RGBA;

            internalFormat =
                GL_RGBA8;

            bytesPerPixel =
                4;

            break;

        case PixelFormat::BGR8:

#ifdef __EMSCRIPTEN__

            return false;

#else

            sourceFormat =
                GL_BGR;

            internalFormat =
                GL_RGB8;

            bytesPerPixel =
                3;

            break;

#endif

        case PixelFormat::BGRA8:

#ifdef __EMSCRIPTEN__

            return false;

#else

            sourceFormat =
                GL_BGRA;

            internalFormat =
                GL_RGBA8;

            bytesPerPixel =
                4;

            break;

#endif

        case PixelFormat::Gray8:
            sourceFormat =
                GL_RED;

            internalFormat =
                GL_R8;

            bytesPerPixel =
                1;

            break;

        default:
            return false;
    }

    if (
        bytesPerPixel == 0
    )
    {
        return false;
    }

    const std::size_t packedRowBytes =
        static_cast<std::size_t>(
            frame.width
        ) *
        bytesPerPixel;

    if (
        frame.stride < packedRowBytes
    )
    {
        std::cerr
            << "IBVAP UI: invalid frame stride | "
               "source="
            << sourceId
            << " | stride="
            << frame.stride
            << " | required="
            << packedRowBytes
            << '\n';

        return false;
    }

    /*
     * OpenGL texture upload expects a tightly packed image
     * when GL_UNPACK_ROW_LENGTH is zero.
     *
     * Some video decoders provide rows with padding, so
     * repack those frames before uploading.
     *
     * This is deliberately platform-independent and works
     * for both native OpenGL and WebGL2.
     */

    const std::uint8_t* uploadData =
        frame.data();

    std::vector<std::uint8_t>
        packedFrame;

    if (
        static_cast<std::size_t>(
            frame.stride
        ) !=
        packedRowBytes
    )
    {
        const std::size_t requiredSize =
            packedRowBytes *
            static_cast<std::size_t>(
                frame.height
            );

        if (
            frame.byteSize() <
            static_cast<std::size_t>(
                frame.stride
            ) *
            static_cast<std::size_t>(
                frame.height
            )
        )
        {
            std::cerr
                << "IBVAP UI: frame buffer is smaller "
                   "than its declared stride/height."
                << " | source="
                << sourceId
                << '\n';

            return false;
        }

        try
        {
            packedFrame.resize(
                requiredSize
            );
        }
        catch (...)
        {
            std::cerr
                << "IBVAP UI: failed to allocate "
                   "temporary packed frame."
                << " | source="
                << sourceId
                << '\n';

            return false;
        }

        const std::uint8_t* source =
            frame.data();

        std::uint8_t* destination =
            packedFrame.data();

        for (
            std::uint32_t y = 0;
            y < frame.height;
            ++y
        )
        {
            std::memcpy(
                destination +
                    static_cast<std::size_t>(y) *
                    packedRowBytes,
                source +
                    static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(
                        frame.stride
                    ),
                packedRowBytes
            );
        }

        uploadData =
            packedFrame.data();
    }
    else
    {
        if (
            frame.byteSize() <
            packedRowBytes *
            static_cast<std::size_t>(
                frame.height
            )
        )
        {
            std::cerr
                << "IBVAP UI: frame buffer is smaller "
                   "than the expected packed image."
                << " | source="
                << sourceId
                << '\n';

            return false;
        }
    }

    VideoTexture& texture =
        m_videoTextures[
            sourceId
        ];

    if (
        texture.texture == 0
    )
    {
        glGenTextures(
            1,
            &texture.texture
        );

        if (
            texture.texture == 0
        )
        {
            std::cerr
                << "IBVAP UI: "
                   "glGenTextures failed."
                << '\n';

            return false;
        }

        glBindTexture(
            GL_TEXTURE_2D,
            texture.texture
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE
        );
    }
    else
    {
        glBindTexture(
            GL_TEXTURE_2D,
            texture.texture
        );
    }

    glPixelStorei(
        GL_UNPACK_ALIGNMENT,
        1
    );

    glPixelStorei(
        GL_UNPACK_ROW_LENGTH,
        0
    );

    const bool sizeChanged =
        texture.width != frame.width ||
        texture.height != frame.height;

    if (
        sizeChanged
    )
    {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            static_cast<GLint>(
                internalFormat
            ),
            static_cast<GLsizei>(
                frame.width
            ),
            static_cast<GLsizei>(
                frame.height
            ),
            0,
            sourceFormat,
            GL_UNSIGNED_BYTE,
            uploadData
        );

        texture.width =
            frame.width;

        texture.height =
            frame.height;
    }
    else
    {
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            static_cast<GLsizei>(
                frame.width
            ),
            static_cast<GLsizei>(
                frame.height
            ),
            sourceFormat,
            GL_UNSIGNED_BYTE,
            uploadData
        );
    }

    const GLenum error =
        glGetError();

    if (
        error != GL_NO_ERROR
    )
    {
        std::cerr
            << "IBVAP UI: "
               "OpenGL texture upload error: 0x"
            << std::hex
            << static_cast<unsigned int>(
                error
            )
            << std::dec
            << " | source="
            << sourceId
            << " | resolution="
            << frame.width
            << "x"
            << frame.height
            << '\n';

        glBindTexture(
            GL_TEXTURE_2D,
            0
        );

        return false;
    }

    glPixelStorei(
        GL_UNPACK_ROW_LENGTH,
        0
    );

    glPixelStorei(
        GL_UNPACK_ALIGNMENT,
        4
    );

    glBindTexture(
        GL_TEXTURE_2D,
        0
    );

    return true;
}


void IBVAPUI::cleanupUnusedTextures()
{
    std::vector<std::string>
        activeSourceIds;

    activeSourceIds.reserve(
        m_videoSlots.size()
    );

    for (
        const VideoSlot& slot :
        m_videoSlots
    )
    {
        if (
            !slot.sourceId.empty()
        )
        {
            activeSourceIds.push_back(
                slot.sourceId
            );
        }
    }

    for (
        auto it =
            m_videoTextures.begin();
        it !=
            m_videoTextures.end();
    )
    {
        const bool stillUsed =
            std::find(
                activeSourceIds.begin(),
                activeSourceIds.end(),
                it->first
            ) !=
            activeSourceIds.end();

        if (
            !stillUsed
        )
        {
            if (
                it->second.texture != 0
            )
            {
                glDeleteTextures(
                    1,
                    &it->second.texture
                );
            }

            it =
                m_videoTextures.erase(
                    it
                );
        }
        else
        {
            ++it;
        }
    }
}


void IBVAPUI::removeVideoSlot(
    std::size_t slotIndex
)
{
    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return;
    }

    VideoSlot& slot =
        m_videoSlots[
            slotIndex
        ];

    if (
        !slot.sourceId.empty()
    )
    {
        clearDashboardTextures(slot);

#ifndef __EMSCRIPTEN__

        removeAISource(
            slot.sourceId
        );

#endif

        m_videoSourceManager.stopSource(
            slot.sourceId
        );

        m_videoSourceManager.removeSource(
            slot.sourceId
        );

        auto textureIt =
            m_videoTextures.find(
                slot.sourceId
            );

        if (
            textureIt !=
            m_videoTextures.end()
        )
        {
            if (
                textureIt->second.texture !=
                0
            )
            {
                glDeleteTextures(
                    1,
                    &textureIt->second.texture
                );
            }

            m_videoTextures.erase(
                textureIt
            );
        }
    }

    m_videoSlots.erase(
        m_videoSlots.begin() +
        static_cast<
            std::vector<
                VideoSlot
            >::difference_type
        >(
            slotIndex
        )
    );

    if (
        m_selectedSlot ==
        static_cast<int>(
            slotIndex
        )
    )
    {
        m_selectedSlot =
            -1;
    }
    else if (
        m_selectedSlot >
        static_cast<int>(
            slotIndex
        )
    )
    {
        --m_selectedSlot;
    }

    if (
        m_fullscreenSlot ==
        static_cast<int>(
            slotIndex
        )
    )
    {
        m_fullscreenSlot =
            -1;
    }
    else if (
        m_fullscreenSlot >
        static_cast<int>(
            slotIndex
        )
    )
    {
        --m_fullscreenSlot;
    }
}


bool IBVAPUI::createRTSPSource(
    std::size_t slotIndex
)
{
    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return false;
    }

    if (
        m_rtspUrl[0] ==
        '\0'
    )
    {
        return false;
    }

    VideoSlot& slot =
        m_videoSlots[
            slotIndex
        ];

    const std::string rtspUrl =
        m_rtspUrl;

    const std::string sourceId =
        m_videoSourceManager.addRTSPStream(
            rtspUrl,
            slot.name
        );

    if (
        sourceId.empty()
    )
    {
        slot.videoError =
            true;

        return false;
    }

    slot.sourceId =
        sourceId;

    if (
        !m_videoSourceManager.startSource(
            sourceId
        )
    )
    {
        slot.videoError =
            true;

        return false;
    }

    return true;
}


bool IBVAPUI::createCameraSource(
    std::size_t slotIndex
)
{
    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return false;
    }

    VideoSlot& slot =
        m_videoSlots[
            slotIndex
        ];

    const std::string sourceId =
        m_videoSourceManager.addCamera(
            0,
            slot.name
        );

    if (
        sourceId.empty()
    )
    {
        slot.videoError =
            true;

        return false;
    }

    slot.sourceId =
        sourceId;

    if (
        !m_videoSourceManager.startSource(
            sourceId
        )
    )
    {
        slot.videoError =
            true;

        return false;
    }

    return true;
}


bool IBVAPUI::createMobileSource(
    std::size_t slotIndex
)
{
    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return false;
    }

    VideoSlot& slot =
        m_videoSlots[
            slotIndex
        ];

    if (
        !m_videoSourceManager
            .isMobileStreamServerRunning()
    )
    {
        slot.videoError =
            true;

        return false;
    }

    const std::string sourceId =
        m_videoSourceManager.addMobileStream(
            slot.name
        );

    if (
        sourceId.empty()
    )
    {
        slot.videoError =
            true;

        return false;
    }

    slot.sourceId =
        sourceId;

    slot.mobileUrl =
        m_videoSourceManager.getMobileStreamUrl(
            sourceId
        );

    if (
        slot.mobileUrl.empty()
    )
    {
        m_videoSourceManager.removeSource(
            sourceId
        );

        slot.sourceId.clear();

        slot.videoError =
            true;

        return false;
    }

    if (
        !generateMobileQrCode(
            slot
        )
    )
    {
        std::cerr
            << "IBVAP UI: failed to generate mobile QR code | source="
            << sourceId
            << '\n';
    }

    if (
        !m_videoSourceManager.startSource(
            sourceId
        )
    )
    {
        slot.videoError =
            true;
    }

    return true;
}


// ==================== END PART 4 / 6 ====================
// ==================== PART 5 / 6 ====================

bool IBVAPUI::generateMobileQrCode(
    VideoSlot& slot
)
{
    slot.qrModules.clear();

    slot.qrVersion =
        0;

    slot.qrSize =
        0;

    slot.qrReady =
        false;

    if (
        slot.mobileUrl.empty()
    )
    {
        return false;
    }

    constexpr std::uint8_t firstVersion =
        5;

    constexpr std::uint8_t lastVersion =
        10;

    for (
        std::uint8_t version =
            firstVersion;
        version <= lastVersion;
        ++version
    )
    {
        const std::size_t bufferSize =
            qrcode_getBufferSize(
                version
            );

        if (
            bufferSize == 0
        )
        {
            continue;
        }

        std::vector<std::uint8_t>
            modules(
                bufferSize
            );

        QRCode qrcode;

        const int8_t result =
            qrcode_initText(
                &qrcode,
                modules.data(),
                version,
                ECC_LOW,
                slot.mobileUrl.c_str()
            );

        if (
            result != 0
        )
        {
            continue;
        }

        if (
            qrcode.size == 0
        )
        {
            continue;
        }

        slot.qrModules =
            std::move(
                modules
            );

        slot.qrVersion =
            version;

        slot.qrSize =
            qrcode.size;

        slot.qrReady =
            true;

        return true;
    }

    return false;
}


void IBVAPUI::renderMobileQrCode(
    const VideoSlot& slot,
    float maximumSize
)
{
    if (
        !slot.qrReady ||
        slot.qrModules.empty() ||
        slot.qrSize == 0
    )
    {
        return;
    }

    if (
        maximumSize <= 0.0f
    )
    {
        return;
    }

    constexpr float quietZone =
        4.0f;

    const float moduleCount =
        static_cast<float>(
            slot.qrSize
        ) +
        quietZone * 2.0f;

    const float moduleSize =
        std::floor(
            maximumSize /
            moduleCount
        );

    if (
        moduleSize < 1.0f
    )
    {
        return;
    }

    const float qrPixelSize =
        moduleCount *
        moduleSize;

    const ImVec2 cursor =
        ImGui::GetCursorScreenPos();

    const float availableWidth =
        ImGui::GetContentRegionAvail().x;

    const float originX =
        cursor.x +
        std::max(
            0.0f,
            (
                availableWidth -
                qrPixelSize
            ) * 0.5f
        );

    const float originY =
        cursor.y;

    ImDrawList* drawList =
        ImGui::GetWindowDrawList();

    drawList->AddRectFilled(
        ImVec2(
            originX,
            originY
        ),
        ImVec2(
            originX + qrPixelSize,
            originY + qrPixelSize
        ),
        IM_COL32(
            255,
            255,
            255,
            255
        )
    );

    QRCode qrcode;

    qrcode.version =
        slot.qrVersion;

    qrcode.size =
        slot.qrSize;

    qrcode.ecc =
        ECC_LOW;

    qrcode.mode =
        MODE_BYTE;

    qrcode.mask =
        0;

    qrcode.modules =
        const_cast<std::uint8_t*>(
            slot.qrModules.data()
        );

    for (
        std::uint8_t y = 0;
        y < slot.qrSize;
        ++y
    )
    {
        for (
            std::uint8_t x = 0;
            x < slot.qrSize;
            ++x
        )
        {
            if (
                !qrcode_getModule(
                    &qrcode,
                    x,
                    y
                )
            )
            {
                continue;
            }

            const float moduleX =
                originX +
                (
                    quietZone +
                    static_cast<float>(x)
                ) *
                moduleSize;

            const float moduleY =
                originY +
                (
                    quietZone +
                    static_cast<float>(y)
                ) *
                moduleSize;

            drawList->AddRectFilled(
                ImVec2(
                    moduleX,
                    moduleY
                ),
                ImVec2(
                    moduleX + moduleSize,
                    moduleY + moduleSize
                ),
                IM_COL32(
                    0,
                    0,
                    0,
                    255
                )
            );
        }
    }

    ImGui::Dummy(
        ImVec2(
            qrPixelSize,
            qrPixelSize
        )
    );
}


void IBVAPUI::copyMobileUrl(
    const std::string& url
)
{
    if (
        url.empty()
    )
    {
        return;
    }

    SDL_SetClipboardText(
        url.c_str()
    );
}


void IBVAPUI::requestVideoFile(
    std::size_t slotIndex
)
{
    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return;
    }

#ifdef __EMSCRIPTEN__

    m_selectedSlot =
        static_cast<int>(
            slotIndex
        );

    ibvap_web_open_video_file_picker();

    return;

#else

    {
        std::lock_guard<std::mutex>
            lock(
                m_fileDialogMutex
            );

        if (
            m_fileDialogOpen
        )
        {
            return;
        }

        m_fileDialogOpen =
            true;

        m_fileDialogResultReady =
            false;

        m_pendingFileSlot =
            slotIndex;

        m_pendingFilePath.clear();

        m_fileDialogError.clear();
    }

    static SDL_DialogFileFilter
        filters[] =
    {
        {
            "Video files",
            "mp4;mkv;avi;mov;webm"
        },
        {
            "All files",
            "*"
        }
    };

    SDL_ShowOpenFileDialog(
        fileDialogCallback,
        this,
        m_window,
        filters,
        2,
        nullptr,
        false
    );

#endif
}


void SDLCALL IBVAPUI::fileDialogCallback(
    void* userdata,
    const char* const* filelist,
    int filter
)
{
    (void)filter;

    IBVAPUI* ui =
        static_cast<IBVAPUI*>(
            userdata
        );

    if (!ui)
    {
        return;
    }

    std::lock_guard<std::mutex>
        lock(
            ui->m_fileDialogMutex
        );

    if (
        filelist == nullptr
    )
    {
        ui->m_fileDialogError =
            "File dialog failed.";

        ui->m_fileDialogResultReady =
            true;

        ui->m_fileDialogOpen =
            false;

        return;
    }

    if (
        filelist[0] == nullptr
    )
    {
        ui->m_fileDialogResultReady =
            true;

        ui->m_fileDialogOpen =
            false;

        return;
    }

    ui->m_pendingFilePath =
        filelist[0];

    ui->m_fileDialogResultReady =
        true;

    ui->m_fileDialogOpen =
        false;
}


void IBVAPUI::processPendingFileSelection()
{
    std::size_t slotIndex =
        0;

    std::string selectedPath;
    std::string displayName;

#ifdef __EMSCRIPTEN__

    char nameBuffer[1024] =
        {};

    char urlBuffer[4096] =
        {};

    if (
        ibvap_web_poll_video_file(
            nameBuffer,
            static_cast<int>(
                sizeof(nameBuffer)
            ),
            urlBuffer,
            static_cast<int>(
                sizeof(urlBuffer)
            )
        ) == 0
    )
    {
        return;
    }

    slotIndex =
        static_cast<std::size_t>(
            m_selectedSlot >= 0
                ? m_selectedSlot
                : 0
        );

    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return;
    }

    selectedPath =
        urlBuffer;

    displayName =
        nameBuffer;

#else

    {
        std::lock_guard<std::mutex>
            lock(
                m_fileDialogMutex
            );

        if (
            !m_fileDialogResultReady
        )
        {
            return;
        }

        slotIndex =
            m_pendingFileSlot;

        selectedPath =
            std::move(
                m_pendingFilePath
            );

        m_pendingFilePath.clear();

        m_fileDialogResultReady =
            false;
    }

    if (
        selectedPath.empty()
    )
    {
        return;
    }

    if (
        slotIndex >=
        m_videoSlots.size()
    )
    {
        return;
    }

    /*
     * SDL provides the selected native path as UTF-8.
     *
     * Use u8path() here instead of directly constructing
     * std::filesystem::path from the UTF-8 byte sequence.
     *
     * This avoids the MinGW filesystem conversion exception
     * that can occur with non-ASCII Windows paths.
     */
    try
    {
        const std::filesystem::path
            path =
                std::filesystem::u8path(
                    selectedPath
                );

        displayName =
            path.filename().string();
    }
    catch (...)
    {
        displayName.clear();
    }

#endif

    if (
        selectedPath.empty()
    )
    {
        return;
    }

    if (
        displayName.empty()
    )
    {
        displayName =
            "Local Video";
    }

    VideoSlot& slot =
        m_videoSlots[
            slotIndex
        ];

    if (
        !slot.sourceId.empty()
    )
    {
#ifndef __EMSCRIPTEN__

        removeAISource(
            slot.sourceId
        );

#endif

        m_videoSourceManager.stopSource(
            slot.sourceId
        );

        m_videoSourceManager.removeSource(
            slot.sourceId
        );

        auto textureIt =
            m_videoTextures.find(
                slot.sourceId
            );

        if (
            textureIt !=
            m_videoTextures.end()
        )
        {
            if (
                textureIt->second.texture !=
                0
            )
            {
                glDeleteTextures(
                    1,
                    &textureIt->second.texture
                );
            }

            m_videoTextures.erase(
                textureIt
            );
        }

        slot.sourceId.clear();

        slot.hasVideo =
            false;

        slot.videoError =
            false;

        slot.aiResult.clear();

        slot.aiAvailable =
            false;

        slot.aiError =
            false;

        slot.aiFrameCounter =
            0;

        slot.aiLastTimestampUs =
            0;

        slot.aiResultGeneration =
            0;

        slot.displayedFrameSequence =
            0;

        slot.aiResultFrameSequence =
            0;

        slot.hasVirtualFence = false;
        slot.fencePlacementMode = false;
        slot.fenceDrawing = false;
        slot.fenceX = 0.0f;
        slot.fenceY = 0.0f;
        slot.fenceWidth = 0.0f;
        slot.fenceHeight = 0.0f;
        slot.previousFenceDetections.clear();

        clearDashboardTextures(slot);
    }

    const std::string sourceId =
        m_videoSourceManager.addLocalVideo(
            selectedPath,
            displayName
        );

    if (
        sourceId.empty()
    )
    {
        slot.videoError =
            true;

        return;
    }

    slot.sourceId =
        sourceId;

    slot.filePath =
        selectedPath;

    slot.name =
        displayName;

    slot.type =
        "Pre-recorded video";

    slot.hasVideo =
        false;

    slot.videoError =
        false;

    slot.qrModules.clear();

    slot.qrVersion =
        0;

    slot.qrSize =
        0;

    slot.qrReady =
        false;

    slot.aiResult.clear();

    slot.aiAvailable =
        false;

    slot.aiError =
        false;

    slot.aiFrameCounter =
        0;

    slot.aiLastTimestampUs =
        0;

    slot.aiResultGeneration =
        0;

    slot.displayedFrameSequence =
        0;

    slot.aiResultFrameSequence =
        0;

    clearDashboardTextures(slot);

    if (
        !m_videoSourceManager.startSource(
            sourceId
        )
    )
    {
        slot.videoError =
            true;

        return;
    }

    m_selectedSlot =
        static_cast<int>(
            slotIndex
        );
}


/* =========================================================
 * AI
 * =========================================================
 */


bool IBVAPUI::initializeAI()
{
#ifndef __EMSCRIPTEN__

    stopAIWorker();

#endif

    m_aiInitialized =
        false;

    m_aiParamPath.clear();

    m_aiBinPath.clear();

    if (
        m_aiEngine != nullptr
    )
    {
        m_aiEngine->shutdown();

        m_aiEngine.reset();
    }

    const std::filesystem::path
        paramPath =
            findAIModelPath(
                "yolov8s.ncnn.param"
            );

    const std::filesystem::path
        binPath =
            findAIModelPath(
                "yolov8s.ncnn.bin"
            );

    try
    {
        m_aiParamPath =
            paramPath.string();

        m_aiBinPath =
            binPath.string();
    }
    catch (...)
    {
        m_aiParamPath.clear();

        m_aiBinPath.clear();

        std::cerr
            << "IBVAP AI: unable to convert model paths."
            << '\n';

        return false;
    }

    std::error_code
        paramError;

    std::error_code
        binError;

    const bool paramExists =
        std::filesystem::exists(
            paramPath,
            paramError
        );

    const bool binExists =
        std::filesystem::exists(
            binPath,
            binError
        );

    if (
        paramError ||
        binError ||
        !paramExists ||
        !binExists
    )
    {
        std::cerr
            << "IBVAP AI: YOLOv8s NCNN model files not found."
            << '\n'
            << "  PARAM: "
            << m_aiParamPath
            << '\n'
            << "  BIN:   "
            << m_aiBinPath
            << '\n';

        return false;
    }

    m_aiEngine =
        std::make_unique<AIEngine>();

    if (
        !m_aiEngine->initialize(
            m_aiParamPath,
            m_aiBinPath
        )
    )
    {
        std::cerr
            << "IBVAP AI: failed to initialize YOLOv8s NCNN detector."
            << '\n';

        m_aiEngine.reset();

        return false;
    }

    m_aiInitialized =
        true;

    std::cout
        << "IBVAP AI: YOLOv8s NCNN detector initialized."
        << '\n'
        << "  PARAM: "
        << m_aiParamPath
        << '\n'
        << "  BIN:   "
        << m_aiBinPath
        << '\n';

#ifndef __EMSCRIPTEN__

    startAIWorker();

#endif

    return true;
}


void IBVAPUI::shutdownAI()
{
#ifndef __EMSCRIPTEN__

    stopAIWorker();

#endif

    if (
        m_aiEngine != nullptr
    )
    {
        m_aiEngine->shutdown();

        m_aiEngine.reset();
    }

    m_aiInitialized =
        false;

    m_aiParamPath.clear();

    m_aiBinPath.clear();

    for (
        VideoSlot& slot :
        m_videoSlots
    )
    {
        slot.aiResult.clear();

        slot.aiAvailable =
            false;

        slot.aiError =
            false;

        slot.aiFrameCounter =
            0;

        slot.aiLastTimestampUs =
            0;

        slot.aiResultGeneration =
            0;

        slot.displayedFrameSequence =
            0;

        slot.aiResultFrameSequence =
            0;
    }
}


void IBVAPUI::updateAI()
{
    /*
     * Native inference runs on the dedicated AI worker.
     *
     * WebAssembly retains the synchronous path because the
     * current WASM configuration does not use pthreads.
     */
}


bool IBVAPUI::runAIForSlot(
    VideoSlot& slot,
    const Frame& frame
)
{
    if (
        !m_aiInitialized ||
        m_aiEngine == nullptr
    )
    {
        return false;
    }

    if (
        !frame.valid()
    )
    {
        return false;
    }

    ++slot.aiFrameCounter;

#ifndef __EMSCRIPTEN__

    /*
     * NATIVE:
     *
     * Never perform inference on the render thread.
     *
     * submitAIFrame() keeps only the newest pending frame
     * for this source.
     *
     * There is no growing AI queue and no accumulated
     * playback latency.
     */
    submitAIFrame(
        slot.sourceId,
        frame
    );

    AIResult latestResult;

    bool available =
        false;

    bool error =
        false;

    std::uint64_t generation =
        slot.aiResultGeneration;

    std::int64_t timestampUs =
        slot.aiLastTimestampUs;

    std::uint64_t frameSequence =
        slot.aiResultFrameSequence;

    Frame analyzedFrame;

    if (
        getLatestAIResult(
            slot.sourceId,
            latestResult,
            available,
            error,
            generation,
            timestampUs,
            frameSequence,
            analyzedFrame
        )
    )
    {
        if (
            generation >
            slot.aiResultGeneration
        )
        {
            /*
             * Always accept the newest completed AI result.
             *
             * The AI worker is asynchronous. The video may
             * already have advanced by the time inference
             * finishes.
             *
             * Preserve the exact frame sequence belonging
             * to this result. renderAIOverlay() performs the
             * final synchronization check before drawing.
             */
            slot.aiResult =
                std::move(
                    latestResult
                );

            slot.aiResultGeneration =
                generation;

            slot.aiAvailable =
                available;

            slot.aiError =
                error;

            slot.aiLastTimestampUs =
                timestampUs;

            slot.aiResultFrameSequence =
                frameSequence;

            if (
                available &&
                !error &&
                analyzedFrame.valid()
            )
            {
                updateDashboardForSlot(
                    slot,
                    slot.aiResult,
                    analyzedFrame
                );
            }
        }
    }

    return slot.aiAvailable;

#else

    /*
     * WASM:
     *
     * Keep the existing synchronous implementation.
     */
    constexpr std::uint64_t
        inferenceInterval = 2;

    if (
        (
            slot.aiFrameCounter %
            inferenceInterval
        ) != 0
    )
    {
        return slot.aiAvailable;
    }

    const AIFrame aiFrame =
        AIFrame::fromFrame(
            frame
        );

    if (
        !aiFrame.valid()
    )
    {
        slot.aiError =
            true;

        return false;
    }

    AIResult result;

    const bool success =
        m_aiEngine->infer(
            aiFrame,
            result
        );

    if (
        !success
    )
    {
        slot.aiError =
            true;

        return false;
    }

    slot.aiResult =
        std::move(
            result
        );

    // renderAIOverlay() consumes results only when this generation changes.
    // Native inference advances it in the worker; the synchronous WebAssembly
    // path must advance it here so its detections reach the overlay too.
    ++slot.aiResultGeneration;

    slot.aiAvailable =
        slot.aiResult.inferenceSucceeded;

    slot.aiError =
        !slot.aiAvailable;

    slot.aiLastTimestampUs =
        frame.timestampUs;

    slot.aiResultFrameSequence =
        frame.sequence;

    if (
        slot.aiAvailable
    )
    {
        updateDashboardForSlot(
            slot,
            slot.aiResult,
            frame
        );
    }

    return slot.aiAvailable;

#endif
}


#ifndef __EMSCRIPTEN__

void IBVAPUI::startAIWorker()
{
    stopAIWorker();

    {
        std::lock_guard<std::mutex>
            lock(
                m_aiWorkerMutex
            );

        m_aiWorkerStopRequested =
            false;

        m_aiWorkerRunning =
            true;
    }

    m_aiWorkerThread =
        std::thread(
            &IBVAPUI::aiWorkerLoop,
            this
        );
}


void IBVAPUI::stopAIWorker() noexcept
{
    {
        std::lock_guard<std::mutex>
            lock(
                m_aiWorkerMutex
            );

        m_aiWorkerStopRequested =
            true;

        m_aiWorkerRunning =
            false;
    }

    m_aiWorkerCondition.notify_all();

    if (
        m_aiWorkerThread.joinable()
    )
    {
        m_aiWorkerThread.join();
    }

    {
        std::lock_guard<std::mutex>
            lock(
                m_aiWorkerMutex
            );

        m_aiLatestFrames.clear();

        m_aiPendingSources.clear();

        m_aiReadySources.clear();

        m_aiResults.clear();

        m_aiActiveSources.clear();

        m_aiWorkerStopRequested =
            false;
    }
}


void IBVAPUI::submitAIFrame(
    const std::string& sourceId,
    const Frame& frame
)
{
    if (
        sourceId.empty() ||
        !frame.valid()
    )
    {
        return;
    }

    {
        std::lock_guard<std::mutex>
            lock(
                m_aiWorkerMutex
            );

        if (
            m_aiWorkerStopRequested ||
            !m_aiWorkerRunning
        )
        {
            return;
        }

        /*
         * Frame uses shared ownership of FrameBuffer.
         *
         * This copies Frame metadata/shared_ptr, not the
         * complete image byte array.
         */
        m_aiLatestFrames[
            sourceId
        ] =
            frame;

        m_aiActiveSources.insert(
            sourceId
        );

        /*
         * At most one scheduler entry exists for a source.
         *
         * Newer frames replace the mailbox contents.
         */
        if (
            m_aiPendingSources.insert(
                sourceId
            ).second
        )
        {
            m_aiReadySources.push_back(
                sourceId
            );
        }
    }

    m_aiWorkerCondition.notify_one();
}


bool IBVAPUI::getLatestAIResult(
    const std::string& sourceId,
    AIResult& result,
    bool& available,
    bool& error,
    std::uint64_t& generation,
    std::int64_t& timestampUs,
    std::uint64_t& frameSequence,
    Frame& frame
)
{
    std::lock_guard<std::mutex>
        lock(
            m_aiWorkerMutex
        );

    const auto it =
        m_aiResults.find(
            sourceId
        );

    if (
        it ==
        m_aiResults.end()
    )
    {
        return false;
    }

    const AIWorkerResult& workerResult =
        it->second;

    generation =
        workerResult.generation;

    available =
        workerResult.available;

    error =
        workerResult.error;

    timestampUs =
        workerResult.timestampUs;

    frameSequence =
        workerResult.frameSequence;

    frame =
        workerResult.frame;

    result =
        workerResult.result;

    return true;
}


void IBVAPUI::removeAISource(
    const std::string& sourceId
)
{
    if (
        sourceId.empty()
    )
    {
        return;
    }

    std::lock_guard<std::mutex>
        lock(
            m_aiWorkerMutex
        );

    m_aiActiveSources.erase(
        sourceId
    );

    m_aiLatestFrames.erase(
        sourceId
    );

    m_aiPendingSources.erase(
        sourceId
    );

    m_aiResults.erase(
        sourceId
    );
}


void IBVAPUI::aiWorkerLoop()
{
    for (;;)
    {
        std::string sourceId;

        Frame frame;

        {
            std::unique_lock<std::mutex>
                lock(
                    m_aiWorkerMutex
                );

            m_aiWorkerCondition.wait(
                lock,
                [this]()
                {
                    return
                        m_aiWorkerStopRequested ||
                        !m_aiReadySources.empty();
                }
            );

            if (
                m_aiWorkerStopRequested
            )
            {
                return;
            }

            if (
                m_aiReadySources.empty()
            )
            {
                continue;
            }

            sourceId =
                std::move(
                    m_aiReadySources.front()
                );

            m_aiReadySources.pop_front();

            m_aiPendingSources.erase(
                sourceId
            );

            const auto activeIt =
                m_aiActiveSources.find(
                    sourceId
                );

            if (
                activeIt ==
                m_aiActiveSources.end()
            )
            {
                continue;
            }

            const auto frameIt =
                m_aiLatestFrames.find(
                    sourceId
                );

            if (
                frameIt ==
                m_aiLatestFrames.end()
            )
            {
                continue;
            }

            frame =
                std::move(
                    frameIt->second
                );

            m_aiLatestFrames.erase(
                frameIt
            );
        }

        if (
            !frame.valid()
        )
        {
            continue;
        }

        AIResult result;

        bool success =
            false;

        /*
         * Do not hold the worker mutex while running ncnn.
         */
        if (
            m_aiEngine != nullptr
        )
        {
            const AIFrame aiFrame =
                AIFrame::fromFrame(
                    frame
                );

            if (
                aiFrame.valid()
            )
            {
                success =
                    m_aiEngine->infer(
                        aiFrame,
                        result
                    );
            }
        }

        {
            std::lock_guard<std::mutex>
                lock(
                    m_aiWorkerMutex
                );

            const auto activeIt =
                m_aiActiveSources.find(
                    sourceId
                );

            if (
                activeIt ==
                m_aiActiveSources.end()
            )
            {
                continue;
            }

            AIWorkerResult& workerResult =
                m_aiResults[
                    sourceId
                ];

            ++workerResult.generation;

            if (
                success &&
                result.inferenceSucceeded
            )
            {
                workerResult.result =
                    std::move(
                        result
                    );

                workerResult.available =
                    true;

                workerResult.error =
                    false;

                workerResult.timestampUs =
                    frame.timestampUs;

                /*
                 * The result belongs to this exact Frame.
                 */
                workerResult.frameSequence =
                    frame.sequence;

                workerResult.frame =
                    frame;
            }
            else
            {
                /*
                 * Preserve the last successful detection result.
                 *
                 * Do not replace its frame sequence when the
                 * newest inference attempt fails.
                 */
                workerResult.error =
                    true;
            }

            /*
             * If a newer frame arrived while inference was
             * executing, submitAIFrame() has already placed
             * this source back into the scheduler.
             */
        }
    }
}

#endif


// ==================== END PART 5 / 6 ====================
// ==================== PART 6 / 6 ====================



void IBVAPUI::clearDashboardTextures(
    VideoSlot& slot
) noexcept
{
    for (
        VideoSlot::DashboardDetection& detection :
        slot.dashboardDetections
    )
    {
        if (
            detection.texture != 0
        )
        {
            glDeleteTextures(
                1,
                &detection.texture
            );

            detection.texture = 0;
        }
    }

    slot.dashboardDetections.clear();
}


bool IBVAPUI::uploadDashboardCrop(
    VideoSlot::DashboardDetection& detection,
    const Frame& frame
)
{
    if (
        !frame.valid() ||
        frame.width == 0 ||
        frame.height == 0
    )
    {
        return false;
    }

    constexpr std::uint32_t cropWidth = 96;
    constexpr std::uint32_t cropHeight = 128;

    const std::uint32_t left =
        static_cast<std::uint32_t>(
            std::clamp(
                detection.x,
                0.0f,
                1.0f
            ) *
            static_cast<float>(frame.width - 1)
        );

    const std::uint32_t top =
        static_cast<std::uint32_t>(
            std::clamp(
                detection.y,
                0.0f,
                1.0f
            ) *
            static_cast<float>(frame.height - 1)
        );

    const std::uint32_t right =
        static_cast<std::uint32_t>(
            std::clamp(
                detection.x + detection.width,
                0.0f,
                1.0f
            ) *
            static_cast<float>(frame.width)
        );

    const std::uint32_t bottom =
        static_cast<std::uint32_t>(
            std::clamp(
                detection.y + detection.height,
                0.0f,
                1.0f
            ) *
            static_cast<float>(frame.height)
        );

    if (
        right <= left ||
        bottom <= top
    )
    {
        return false;
    }

    const std::uint32_t cropSourceWidth =
        right - left;

    const std::uint32_t cropSourceHeight =
        bottom - top;

    std::vector<std::uint8_t> pixels;

    try
    {
        pixels.resize(
            static_cast<std::size_t>(cropWidth) *
            static_cast<std::size_t>(cropHeight) *
            3
        );
    }
    catch (...)
    {
        return false;
    }

    const std::uint8_t* source =
        frame.data();

    if (source == nullptr)
    {
        return false;
    }

    std::size_t sourceBytesPerPixel = 0;

    switch (frame.format)
    {
        case PixelFormat::RGB8:
        case PixelFormat::BGR8:
            sourceBytesPerPixel = 3;
            break;

        case PixelFormat::RGBA8:
        case PixelFormat::BGRA8:
            sourceBytesPerPixel = 4;
            break;

        case PixelFormat::Gray8:
            sourceBytesPerPixel = 1;
            break;

        default:
            return false;
    }

    const std::size_t minimumRowBytes =
        static_cast<std::size_t>(frame.width) *
        sourceBytesPerPixel;

    if (
        frame.stride < minimumRowBytes ||
        frame.byteSize() <
            static_cast<std::size_t>(frame.stride) *
            static_cast<std::size_t>(frame.height)
    )
    {
        return false;
    }

    for (
        std::uint32_t y = 0;
        y < cropHeight;
        ++y
    )
    {
        const std::uint32_t sourceY =
            top +
            std::min(
                cropSourceHeight - 1,
                static_cast<std::uint32_t>(
                    (
                        static_cast<std::uint64_t>(y) *
                        cropSourceHeight
                    ) /
                    cropHeight
                )
            );

        const std::uint8_t* sourceRow =
            source +
            static_cast<std::size_t>(sourceY) *
            static_cast<std::size_t>(frame.stride);

        for (
            std::uint32_t x = 0;
            x < cropWidth;
            ++x
        )
        {
            const std::uint32_t sourceX =
                left +
                std::min(
                    cropSourceWidth - 1,
                    static_cast<std::uint32_t>(
                        (
                            static_cast<std::uint64_t>(x) *
                            cropSourceWidth
                        ) /
                        cropWidth
                    )
                );

            const std::uint8_t* pixel =
                sourceRow +
                static_cast<std::size_t>(sourceX) *
                sourceBytesPerPixel;

            std::uint8_t* destination =
                pixels.data() +
                (
                    static_cast<std::size_t>(y) *
                    cropWidth +
                    x
                ) *
                3;

            switch (frame.format)
            {
                case PixelFormat::RGB8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[1];
                    destination[2] = pixel[2];
                    break;

                case PixelFormat::BGR8:
                    destination[0] = pixel[2];
                    destination[1] = pixel[1];
                    destination[2] = pixel[0];
                    break;

                case PixelFormat::RGBA8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[1];
                    destination[2] = pixel[2];
                    break;

                case PixelFormat::BGRA8:
                    destination[0] = pixel[2];
                    destination[1] = pixel[1];
                    destination[2] = pixel[0];
                    break;

                case PixelFormat::Gray8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[0];
                    destination[2] = pixel[0];
                    break;

                default:
                    return false;
            }
        }
    }

    detection.image.pixels = pixels;
    detection.image.width = cropWidth;
    detection.image.height = cropHeight;
    detection.image.channels = 3;

    if (
        detection.texture == 0
    )
    {
        glGenTextures(
            1,
            &detection.texture
        );

        if (
            detection.texture == 0
        )
        {
            return false;
        }

        glBindTexture(
            GL_TEXTURE_2D,
            detection.texture
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE
        );
    }
    else
    {
        glBindTexture(
            GL_TEXTURE_2D,
            detection.texture
        );
    }

    glPixelStorei(
        GL_UNPACK_ALIGNMENT,
        1
    );

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB8,
        static_cast<GLsizei>(cropWidth),
        static_cast<GLsizei>(cropHeight),
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );

    const GLenum error =
        glGetError();

    glBindTexture(
        GL_TEXTURE_2D,
        0
    );

    glPixelStorei(
        GL_UNPACK_ALIGNMENT,
        4
    );

    if (
        error != GL_NO_ERROR
    )
    {
        return false;
    }

    detection.textureWidth = cropWidth;
    detection.textureHeight = cropHeight;

    return true;
}


void IBVAPUI::updateDashboardForSlot(
    VideoSlot& slot,
    const AIResult& result,
    const Frame& analyzedFrame
)
{
    constexpr float minimumConfidence = 0.40f;
    constexpr float minimumIoU = 0.30f;
    constexpr std::uint32_t requiredStableFrames = 2;

    if (
        !result.inferenceSucceeded ||
        result.inputWidth == 0 ||
        result.inputHeight == 0 ||
        !analyzedFrame.valid()
    )
    {
        return;
    }

    // Detections that do not qualify for a dashboard card still produce a
    // concise unknown-entity alert instead of disappearing silently.
    for (const AIDetection& detection : result.detections)
    {
        const bool dashboardClass =
            detection.classId == AIObjectClass::Person ||
            detection.classId == AIObjectClass::Vehicle ||
            detection.classId == AIObjectClass::LicensePlate;
        const float minimumConfidence =
            detection.classId == AIObjectClass::LicensePlate ? 0.30f : 0.40f;
        if (!dashboardClass || detection.confidence < minimumConfidence)
        {
            queueAlert(
                "UNKNOWN ENTITY DETECTED",
                slot.sourceId,
                IM_COL32(235, 155, 45, 255)
            );
        }
    }

    std::vector<bool> oldUsed(
        slot.dashboardDetections.size(),
        false
    );

    std::vector<VideoSlot::DashboardDetection> updated;
    updated.reserve(
        result.detections.size()
    );

    auto intersectionOverUnion =
        [](float ax, float ay, float aw, float ah,
           float bx, float by, float bw, float bh) -> float
        {
            const float aRight = ax + aw;
            const float aBottom = ay + ah;
            const float bRight = bx + bw;
            const float bBottom = by + bh;

            const float left = std::max(ax, bx);
            const float top = std::max(ay, by);
            const float right = std::min(aRight, bRight);
            const float bottom = std::min(aBottom, bBottom);

            if (right <= left || bottom <= top)
            {
                return 0.0f;
            }

            const float intersection =
                (right - left) *
                (bottom - top);

            const float unionArea =
                aw * ah +
                bw * bh -
                intersection;

            return unionArea > 0.0f
                ? intersection / unionArea
                : 0.0f;
        };

    for (
        const AIDetection& detection :
        result.detections
    )
    {
        if (
            detection.classId != AIObjectClass::Person &&
            detection.classId != AIObjectClass::Vehicle &&
            detection.classId != AIObjectClass::LicensePlate
        )
        {
            continue;
        }

        const float detectionMinimumConfidence =
            detection.classId == AIObjectClass::LicensePlate
                ? 0.30f
                : minimumConfidence;

        if (
            detection.confidence < detectionMinimumConfidence
        )
        {
            continue;
        }

        const float x =
            std::clamp(
                detection.boundingBox.x /
                    static_cast<float>(result.inputWidth),
                0.0f,
                1.0f
            );

        const float y =
            std::clamp(
                detection.boundingBox.y /
                    static_cast<float>(result.inputHeight),
                0.0f,
                1.0f
            );

        const float width =
            std::clamp(
                detection.boundingBox.width /
                    static_cast<float>(result.inputWidth),
                0.0f,
                1.0f - x
            );

        const float height =
            std::clamp(
                detection.boundingBox.height /
                    static_cast<float>(result.inputHeight),
                0.0f,
                1.0f - y
            );

        if (width <= 0.0f || height <= 0.0f)
        {
            continue;
        }

        std::size_t bestIndex =
            slot.dashboardDetections.size();

        float bestIoU = 0.0f;

        for (
            std::size_t i = 0;
            i < slot.dashboardDetections.size();
            ++i
        )
        {
            if (oldUsed[i])
            {
                continue;
            }

            const auto& previous =
                slot.dashboardDetections[i];

            if (
                previous.classId != detection.classId
            )
            {
                continue;
            }

            const float iou =
                intersectionOverUnion(
                    x, y, width, height,
                    previous.x,
                    previous.y,
                    previous.width,
                    previous.height
                );

            if (
                iou >= minimumIoU &&
                iou > bestIoU
            )
            {
                bestIoU = iou;
                bestIndex = i;
            }
        }

        VideoSlot::DashboardDetection current;

        bool wasVisible = false;

        if (
            bestIndex < slot.dashboardDetections.size()
        )
        {
            oldUsed[bestIndex] = true;
            current =
                std::move(
                    slot.dashboardDetections[bestIndex]
                );

            wasVisible =
                current.visible;

            current.stableFrames =
                std::min<std::uint32_t>(
                    current.stableFrames + 1,
                    requiredStableFrames
                );
        }
        else
        {
            current.stableFrames = 1;
        }

        current.classId =
            detection.classId;

        current.confidence =
            detection.confidence;

        current.x = x;
        current.y = y;
        current.width = width;
        current.height = height;
        current.lastFrameSequence =
            analyzedFrame.sequence;
        current.visible =
            current.stableFrames >= requiredStableFrames;

        if (
            !current.visible &&
            (current.identityId <= 0 || current.identityName.empty())
        )
        {
            queueAlert(
                "UNKNOWN ENTITY DETECTED",
                slot.sourceId,
                IM_COL32(235, 155, 45, 255)
            );
        }

        /*
         * Store the wall-clock moment when this detection first
         * becomes visible on the dashboard.  Do not update this
         * value on every AI result, otherwise the card would show
         * a running clock instead of the original detection time.
         */
        if (
            current.visible &&
            !wasVisible
        )
        {
            const auto detectionNow =
                std::chrono::system_clock::now();

            current.lastDetectedAtUs =
                std::chrono::duration_cast<
                    std::chrono::microseconds
                >(
                    detectionNow.time_since_epoch()
                ).count();

            const bool knownEntity =
                current.identityId > 0 && !current.identityName.empty();
            if (!knownEntity)
            {
                queueAlert(
                    "UNKNOWN ENTITY DETECTED",
                    slot.sourceId,
                    IM_COL32(235, 155, 45, 255)
                );
            }
            else if (current.identityType == DatabaseIdentityType::Ally)
            {
                queueAlert(
                    "ALLY DETECTED",
                    slot.sourceId,
                    IM_COL32(45, 215, 100, 255)
                );
            }
            else if (current.identityType == DatabaseIdentityType::Criminal)
            {
                queueAlert(
                    "ENEMY DETECTED",
                    slot.sourceId,
                    IM_COL32(245, 45, 45, 255)
                );
            }
            else
            {
                queueAlert(
                    "SPECIAL ENTITY DETECTED",
                    slot.sourceId,
                    IM_COL32(185, 105, 245, 255)
                );
            }
        }

        if (
            current.visible
        )
        {
            uploadDashboardCrop(
                current,
                analyzedFrame
            );

            if (
                !current.historySaved &&
                !current.image.pixels.empty()
            )
            {
                /*
                 * Store the best SCRFD face crop that lies inside
                 * this person box.  The raw RGB crop is deliberately
                 * used so the database layer stays independent of
                 * OpenCV and remains usable in WASM.
                 */
                float bestFaceIoU = 0.0f;

                for (
                    const AIDetection& face :
                    result.detections
                )
                {
                    if (
                        current.classId != AIObjectClass::Person ||
                        face.classId != AIObjectClass::Face
                    )
                    {
                        continue;
                    }

                    const float faceX =
                        std::clamp(
                            face.boundingBox.x /
                                static_cast<float>(result.inputWidth),
                            0.0f,
                            1.0f
                        );

                    const float faceY =
                        std::clamp(
                            face.boundingBox.y /
                                static_cast<float>(result.inputHeight),
                            0.0f,
                            1.0f
                        );

                    const float faceW =
                        std::clamp(
                            face.boundingBox.width /
                                static_cast<float>(result.inputWidth),
                            0.0f,
                            1.0f - faceX
                        );

                    const float faceH =
                        std::clamp(
                            face.boundingBox.height /
                                static_cast<float>(result.inputHeight),
                            0.0f,
                            1.0f - faceY
                        );

                    const float faceIoU =
                        intersectionOverUnion(
                            current.x,
                            current.y,
                            current.width,
                            current.height,
                            faceX,
                            faceY,
                            faceW,
                            faceH
                        );

                    if (
                        faceIoU > bestFaceIoU &&
                        face.confidence >= 0.40f
                    )
                    {
                        DatabaseImage faceImage;

                        if (
                            captureImage(
                                analyzedFrame,
                                face.boundingBox,
                                faceImage,
                                96,
                                96
                            )
                        )
                        {
                            bestFaceIoU = faceIoU;
                            current.faceImage =
                                std::move(faceImage);
                        }
                    }
                }

                saveDashboardDetectionToHistory(
                    slot,
                    current
                );
            }
        }

        updated.push_back(
            std::move(current)
        );
    }

    /*
     * Any unmatched detection failed the consecutive-result
     * requirement.  Release its texture instead of allowing a stale
     * identity card to remain on the dashboard.
     */
    for (
        std::size_t i = 0;
        i < slot.dashboardDetections.size();
        ++i
    )
    {
        if (oldUsed[i])
        {
            continue;
        }

        auto& stale =
            slot.dashboardDetections[i];

        if (stale.texture != 0)
        {
            glDeleteTextures(
                1,
                &stale.texture
            );
        }
    }

    slot.dashboardDetections =
        std::move(updated);
}



bool IBVAPUI::captureImage(
    const Frame& frame,
    const AIBoundingBox& boundingBox,
    DatabaseImage& image,
    std::uint32_t outputWidth,
    std::uint32_t outputHeight
) const
{
    if (
        !frame.valid() ||
        frame.width == 0 ||
        frame.height == 0 ||
        outputWidth == 0 ||
        outputHeight == 0
    )
    {
        return false;
    }

    const float normalizedX =
        std::clamp(boundingBox.x, 0.0f, 1.0f);

    const float normalizedY =
        std::clamp(boundingBox.y, 0.0f, 1.0f);

    const float normalizedRight =
        std::clamp(
            boundingBox.x + boundingBox.width,
            0.0f,
            1.0f
        );

    const float normalizedBottom =
        std::clamp(
            boundingBox.y + boundingBox.height,
            0.0f,
            1.0f
        );

    const std::uint32_t left =
        static_cast<std::uint32_t>(
            normalizedX * static_cast<float>(frame.width - 1)
        );

    const std::uint32_t top =
        static_cast<std::uint32_t>(
            normalizedY * static_cast<float>(frame.height - 1)
        );

    const std::uint32_t right =
        static_cast<std::uint32_t>(
            normalizedRight * static_cast<float>(frame.width)
        );

    const std::uint32_t bottom =
        static_cast<std::uint32_t>(
            normalizedBottom * static_cast<float>(frame.height)
        );

    if (right <= left || bottom <= top)
    {
        return false;
    }

    std::size_t sourceBytesPerPixel = 0;

    switch (frame.format)
    {
        case PixelFormat::RGB8:
        case PixelFormat::BGR8:
            sourceBytesPerPixel = 3;
            break;

        case PixelFormat::RGBA8:
        case PixelFormat::BGRA8:
            sourceBytesPerPixel = 4;
            break;

        case PixelFormat::Gray8:
            sourceBytesPerPixel = 1;
            break;

        default:
            return false;
    }

    if (
        frame.stride <
            static_cast<std::uint32_t>(frame.width * sourceBytesPerPixel) ||
        frame.byteSize() <
            static_cast<std::size_t>(frame.stride) * frame.height
    )
    {
        return false;
    }

    const std::uint32_t sourceWidth = right - left;
    const std::uint32_t sourceHeight = bottom - top;

    image.width = outputWidth;
    image.height = outputHeight;
    image.channels = 3;

    try
    {
        image.pixels.resize(
            static_cast<std::size_t>(outputWidth) *
            outputHeight *
            3
        );
    }
    catch (...)
    {
        image = {};
        return false;
    }

    const std::uint8_t* source = frame.data();

    if (source == nullptr)
    {
        image = {};
        return false;
    }

    for (std::uint32_t y = 0; y < outputHeight; ++y)
    {
        const std::uint32_t sourceY =
            top +
            std::min(
                sourceHeight - 1,
                static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(y) * sourceHeight) /
                    outputHeight
                )
            );

        const std::uint8_t* sourceRow =
            source +
            static_cast<std::size_t>(sourceY) * frame.stride;

        for (std::uint32_t x = 0; x < outputWidth; ++x)
        {
            const std::uint32_t sourceX =
                left +
                std::min(
                    sourceWidth - 1,
                    static_cast<std::uint32_t>(
                        (static_cast<std::uint64_t>(x) * sourceWidth) /
                        outputWidth
                    )
                );

            const std::uint8_t* pixel =
                sourceRow +
                static_cast<std::size_t>(sourceX) * sourceBytesPerPixel;

            std::uint8_t* destination =
                image.pixels.data() +
                (
                    static_cast<std::size_t>(y) * outputWidth + x
                ) * 3;

            switch (frame.format)
            {
                case PixelFormat::RGB8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[1];
                    destination[2] = pixel[2];
                    break;

                case PixelFormat::BGR8:
                    destination[0] = pixel[2];
                    destination[1] = pixel[1];
                    destination[2] = pixel[0];
                    break;

                case PixelFormat::RGBA8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[1];
                    destination[2] = pixel[2];
                    break;

                case PixelFormat::BGRA8:
                    destination[0] = pixel[2];
                    destination[1] = pixel[1];
                    destination[2] = pixel[0];
                    break;

                case PixelFormat::Gray8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[0];
                    destination[2] = pixel[0];
                    break;

                default:
                    image = {};
                    return false;
            }
        }
    }

    return true;
}

void IBVAPUI::saveDashboardDetectionToHistory(
    const VideoSlot& slot,
    VideoSlot::DashboardDetection& detection
)
{
    if (
        detection.historySaved ||
        m_database == nullptr ||
        !m_database->isReady() ||
        detection.image.pixels.empty()
    )
    {
        return;
    }

    HistoryRecord record;

    record.detectedAtUs =
        detection.lastDetectedAtUs;

    record.sourceId =
        slot.sourceId;

    record.sourceName =
        slot.name.empty()
            ? slot.sourceId
            : slot.name;

    if (detection.classId == AIObjectClass::Vehicle)
    {
        record.objectType = DatabaseObjectType::Vehicle;
    }
    else if (detection.classId == AIObjectClass::LicensePlate)
    {
        record.objectType = DatabaseObjectType::LicensePlate;
    }
    else
    {
        record.objectType = DatabaseObjectType::Person;
    }

    record.confidence =
        detection.confidence;

    record.image =
        detection.image;

    record.faceImage =
        detection.faceImage;

    if (
        record.detectedAtUs <= 0
    )
    {
        record.detectedAtUs =
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
    }

    std::int64_t historyId = 0;

    if (
        m_database->insertHistory(
            record,
            historyId
        )
    )
    {
        detection.historyId = historyId;
        detection.historySaved = true;

        refreshHistory();
    }
}

void IBVAPUI::clearHistoryTextures() noexcept
{
    for (
        auto& pair : m_historyTextures
    )
    {
        if (pair.second != 0)
        {
            glDeleteTextures(
                1,
                &pair.second
            );
        }
    }

    m_historyTextures.clear();
}

void IBVAPUI::refreshHistory()
{
    if (
        m_database == nullptr ||
        !m_database->isReady()
    )
    {
        return;
    }

    std::vector<HistoryRecord> records;

    if (
        !m_database->listHistory(
            records,
            200
        )
    )
    {
        return;
    }

    clearHistoryTextures();
    m_historyRecords = std::move(records);

    /*
     * History is the persistent source of truth for identity status.
     * If a live dashboard detection has already been saved to history
     * and that history record is later enrolled as Ally/Criminal,
     * immediately propagate the identity back to the live detection.
     *
     * This does not touch SCRFD or the face detector. It only connects
     * the existing database identity assignment to the dashboard color.
     */
    for (
        VideoSlot& slot :
        m_videoSlots
    )
    {
        for (
            VideoSlot::DashboardDetection& detection :
            slot.dashboardDetections
        )
        {
            detection.identityId = 0;
            detection.identityName.clear();

            if (detection.historyId <= 0)
            {
                continue;
            }

            const auto historyIt =
                std::find_if(
                    m_historyRecords.begin(),
                    m_historyRecords.end(),
                    [&detection](const HistoryRecord& record)
                    {
                        return record.id == detection.historyId;
                    }
                );

            if (historyIt != m_historyRecords.end() &&
                historyIt->identityId > 0 &&
                !historyIt->identityName.empty())
            {
                detection.identityId = historyIt->identityId;
                detection.identityType = historyIt->identityType;
                detection.identityName = historyIt->identityName;
            }
        }
    }

    for (
        const HistoryRecord& record :
        m_historyRecords
    )
    {
        if (
            record.image.width == 0 ||
            record.image.height == 0 ||
            record.image.channels != 3 ||
            record.image.pixels.empty()
        )
        {
            continue;
        }

        unsigned int texture = 0;

        glGenTextures(
            1,
            &texture
        );

        if (texture == 0)
        {
            continue;
        }

        glBindTexture(
            GL_TEXTURE_2D,
            texture
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE
        );

        glPixelStorei(
            GL_UNPACK_ALIGNMENT,
            1
        );

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB8,
            static_cast<GLsizei>(record.image.width),
            static_cast<GLsizei>(record.image.height),
            0,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            record.image.pixels.data()
        );

        glBindTexture(
            GL_TEXTURE_2D,
            0
        );

        glPixelStorei(
            GL_UNPACK_ALIGNMENT,
            4
        );

        if (glGetError() == GL_NO_ERROR)
        {
            m_historyTextures.emplace(
                record.id,
                texture
            );
        }
        else
        {
            glDeleteTextures(
                1,
                &texture
            );
        }
    }
}

void IBVAPUI::createIdentityFromSelectedHistory(
    DatabaseIdentityType type
)
{
    if (
        m_database == nullptr ||
        !m_database->isReady() ||
        m_selectedHistoryId <= 0 ||
        m_identityName[0] == '\0'
    )
    {
        return;
    }

    auto it = std::find_if(
        m_historyRecords.begin(),
        m_historyRecords.end(),
        [this](const HistoryRecord& record)
        {
            return record.id == m_selectedHistoryId;
        }
    );

    if (it == m_historyRecords.end())
    {
        return;
    }

    DatabaseImage faceImage =
        it->faceImage;

    /*
     * If SCRFD did not produce a face crop for this historical
     * detection, keep the person crop as the enrollment image.
     * A future recognition pass can replace it with a true face
     * embedding once the embedding model is connected.
     */
    if (faceImage.pixels.empty())
    {
        faceImage = it->image;
    }

    std::int64_t identityId = 0;

    if (
        m_database->createIdentity(
            type,
            m_identityName,
            faceImage,
            {},
            identityId
        )
    )
    {
        m_database->assignHistoryIdentity(
            it->id,
            identityId
        );

        for (VideoSlot& slot : m_videoSlots)
        {
            for (VideoSlot::DashboardDetection& detection : slot.dashboardDetections)
            {
                if (detection.historyId == it->id)
                {
                    detection.identityId = identityId;
                    detection.identityType = type;
                    detection.identityName = m_identityName;
                }
            }
        }

        m_selectedHistoryId = 0;
        m_identityName[0] = '\0';
        m_showIdentityPopup = false;
        ImGui::CloseCurrentPopup();

        refreshHistory();
    }
}

void IBVAPUI::renderHistory()
{
    ImGui::TextColored(
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
        "DETECTION HISTORY"
    );

    ImGui::SameLine();
    ImGui::TextDisabled(
        "Persistent records"
    );

    ImGui::Separator();

    if (
        m_database == nullptr ||
        !m_database->isReady()
    )
    {
        ImGui::TextDisabled(
            "Database is initializing..."
        );
        return;
    }

    if (m_historyRecords.empty())
    {
        ImGui::TextDisabled(
            "No detections have been saved yet."
        );
        return;
    }

    for (
        const HistoryRecord& record :
        m_historyRecords
    )
    {
        ImGui::PushID(
            static_cast<int>(record.id)
        );

        const bool selected =
            m_selectedHistoryId == record.id;

        ImGui::BeginChild(
            "HistoryCard",
            ImVec2(0.0f, 132.0f),
            true
        );

        auto textureIt =
            m_historyTextures.find(record.id);

        if (
            textureIt != m_historyTextures.end()
        )
        {
            ImGui::Image(
                static_cast<ImTextureID>(textureIt->second),
                ImVec2(82.0f, 110.0f)
            );
        }

        ImGui::SameLine();
        ImGui::BeginGroup();

        const char* objectName =
            record.objectType == DatabaseObjectType::Vehicle
                ? "Vehicle"
                : (record.objectType == DatabaseObjectType::LicensePlate
                    ? "License Plate"
                    : "Person");

        ImGui::TextColored(
            ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
            "%s",
            objectName
        );

        if (record.identityId > 0 && !record.identityName.empty())
        {
            const ImVec4 identityColor =
                record.identityType == DatabaseIdentityType::Criminal
                    ? ImVec4(0.95f, 0.25f, 0.25f, 1.00f)
                    : (record.identityType == DatabaseIdentityType::Special
                        ? ImVec4(0.70f, 0.42f, 0.95f, 1.00f)
                        : ImVec4(0.25f, 0.95f, 0.40f, 1.00f));

            ImGui::TextColored(
                identityColor,
                "%s",
                record.identityName.c_str()
            );
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
                "UNKNOWN"
            );
        }

        ImGui::TextDisabled(
            "Confidence %.0f%%",
            record.confidence * 100.0f
        );

        ImGui::TextDisabled(
            "Source: %s",
            record.sourceName.empty()
                ? "Unknown"
                : record.sourceName.c_str()
        );

        const auto timePoint =
            std::chrono::system_clock::time_point(
                std::chrono::microseconds(record.detectedAtUs)
            );

        const std::time_t timeValue =
            std::chrono::system_clock::to_time_t(timePoint);

        std::tm localTime{};

#if defined(_WIN32)
        localtime_s(&localTime, &timeValue);
#else
        localtime_r(&timeValue, &localTime);
#endif

        char timeBuffer[64] = {};
        std::strftime(
            timeBuffer,
            sizeof(timeBuffer),
            "%d/%m/%Y %H:%M:%S",
            &localTime
        );

        ImGui::TextDisabled(
            "%s",
            timeBuffer
        );

        if (ImGui::Button(selected ? "SELECTED" : "SELECT"))
        {
            m_selectedHistoryId = record.id;
        }

        ImGui::SameLine();

        if (record.objectType == DatabaseObjectType::Person)
        {
            if (ImGui::Button("ALLY", ImVec2(48.0f, 0.0f)))
            {
                m_selectedHistoryId = record.id;
                m_pendingIdentityType = DatabaseIdentityType::Ally;
                m_identityName[0] = '\0';
                m_showIdentityPopup = true;
            }

            ImGui::SameLine();

            if (ImGui::Button("ENEMY", ImVec2(58.0f, 0.0f)))
            {
                m_selectedHistoryId = record.id;
                m_pendingIdentityType = DatabaseIdentityType::Criminal;
                m_identityName[0] = '\0';
                m_showIdentityPopup = true;
            }

            ImGui::SameLine();

            if (ImGui::Button("SPECIAL", ImVec2(68.0f, 0.0f)))
            {
                m_selectedHistoryId = record.id;
                m_pendingIdentityType = DatabaseIdentityType::Special;
                m_identityName[0] = '\0';
                m_showIdentityPopup = true;
            }
        }

        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::PopID();
    }

    if (m_showIdentityPopup)
    {
        ImGui::OpenPopup("Add Identity");
        m_showIdentityPopup = false;
    }

    if (
        ImGui::BeginPopupModal(
            "Add Identity",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        ImGui::Text(
            "%s name",
            m_pendingIdentityType == DatabaseIdentityType::Criminal
                ? "Enemy"
                : (m_pendingIdentityType == DatabaseIdentityType::Special
                    ? "Special"
                    : "Ally")
        );

        ImGui::InputText(
            "Name",
            m_identityName,
            sizeof(m_identityName)
        );

        if (ImGui::Button("SAVE", ImVec2(100.0f, 0.0f)))
        {
            createIdentityFromSelectedHistory(
                m_pendingIdentityType
            );
        }

        ImGui::SameLine();

        if (ImGui::Button("CANCEL", ImVec2(100.0f, 0.0f)))
        {
            m_identityName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void IBVAPUI::renderDashboard()
{
    ImGui::TextColored(
        ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
        "DETECTION DASHBOARD"
    );

    ImGui::Separator();

    if (
        m_videoSlots.empty()
    )
    {
        ImGui::TextDisabled(
            "No active sources."
        );
        return;
    }

    bool hasCards = false;

    auto formatTime =
        [](std::int64_t timestampUs) -> std::string
        {
            if (timestampUs <= 0)
            {
                return "--:--:--";
            }

            const auto detectionTimePoint =
                std::chrono::system_clock::time_point(
                    std::chrono::microseconds(
                        timestampUs
                    )
                );

            const std::time_t detectionTime =
                std::chrono::system_clock::to_time_t(
                    detectionTimePoint
                );

            std::tm localTime{};

#if defined(_WIN32)
            localtime_s(
                &localTime,
                &detectionTime
            );
#else
            localtime_r(
                &detectionTime,
                &localTime
            );
#endif

            char buffer[32] = {};

            std::strftime(
                buffer,
                sizeof(buffer),
                "%H:%M:%S",
                &localTime
            );

            return std::string(buffer);
        };

    for (
        const VideoSlot& slot :
        m_videoSlots
    )
    {
        for (
            const VideoSlot::DashboardDetection& detection :
            slot.dashboardDetections
        )
        {
            if (
                !detection.visible ||
                detection.texture == 0
            )
            {
                continue;
            }

            hasCards = true;

            ImGui::PushID(
                &detection
            );

            ImGui::BeginChild(
                "Card",
                ImVec2(0.0f, 136.0f),
                true
            );

            const float cropWidth = 82.0f;
            const float cropHeight = 110.0f;

            ImGui::Image(
                static_cast<ImTextureID>(
                    detection.texture
                ),
                ImVec2(
                    cropWidth,
                    cropHeight
                )
            );

            ImGui::SameLine();

            ImGui::BeginGroup();

            ImGui::TextColored(
                ImVec4(0.20f, 0.85f, 0.90f, 1.00f),
                "%s",
                aiObjectClassName(
                    detection.classId
                )
            );

            const bool hasIdentity =
                detection.identityId > 0 &&
                !detection.identityName.empty();

            const ImVec4 identityColor =
                !hasIdentity
                    ? ImVec4(0.20f, 0.85f, 0.90f, 1.00f)
                    : (detection.identityType == DatabaseIdentityType::Criminal
                        ? ImVec4(0.95f, 0.20f, 0.20f, 1.00f)
                        : (detection.identityType == DatabaseIdentityType::Special
                            ? ImVec4(0.70f, 0.42f, 0.95f, 1.00f)
                            : ImVec4(0.20f, 0.95f, 0.35f, 1.00f)));

            ImGui::TextColored(
                identityColor,
                "%s",
                hasIdentity
                    ? detection.identityName.c_str()
                    : "UNKNOWN"
            );

            ImGui::TextDisabled(
                "Confidence %.0f%%",
                detection.confidence * 100.0f
            );

            ImGui::Spacing();

            ImGui::TextDisabled(
                "Time  %s",
                formatTime(
                    detection.lastDetectedAtUs
                ).c_str()
            );

            ImGui::TextDisabled(
                "Source"
            );

            ImGui::TextWrapped(
                "%s",
                slot.name.empty()
                    ? "Unknown source"
                    : slot.name.c_str()
            );

            ImGui::EndGroup();
            ImGui::EndChild();

            ImGui::PopID();

            ImGui::Spacing();
        }
    }

    if (!hasCards)
    {
        ImGui::TextDisabled(
            "Waiting for stable detections..."
        );

        ImGui::TextDisabled(
            "2 consecutive results / 40%% confidence"
        );
    }
}


void IBVAPUI::renderAIOverlay(
    VideoSlot& slot
)
{
    if (
        (!m_aiInitialized || !slot.aiAvailable || slot.aiResult.detections.empty()) &&
        !slot.hasVirtualFence &&
        !slot.fencePlacementMode
    )
    {
        return;
    }

    /*
     * AI inference is asynchronous.  The displayed video must
     * never wait for inference, so the newest completed result can
     * naturally belong to an earlier frame.
     *
     * The previous implementation could make the overlay disappear
     * completely when the displayed sequence moved more than a small
     * number of frames beyond the AI result.  That is not acceptable:
     * a valid detection must remain visible while the video continues.
     *
     * We therefore keep the latest result visible and apply only a
     * small, bounded motion projection.  The projection is smoothed
     * and the box size is NOT extrapolated.  This keeps the box stable
     * without adding a tracker class, queue, delay, or platform-specific
     * code.
     */

    struct StableDetection
    {
        AIObjectClass classId = AIObjectClass::Unknown;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        std::uint32_t stableFrames = 1;
        std::int64_t identityId = 0;
        DatabaseIdentityType identityType = DatabaseIdentityType::Ally;
        std::string identityName;
        bool initialized = false;
    };

    struct OverlayState
    {
        std::uint64_t generation = 0;
        std::uint64_t sequence = 0;
        AIResult result;
        std::vector<StableDetection> stable;
    };

    static std::unordered_map<
        std::string,
        OverlayState
    > states;

    OverlayState& state =
        states[slot.sourceId];

    /*
     * Update the persistent overlay state only when a new inference
     * result arrives.  Rendering the same result on subsequent UI
     * frames must not repeatedly smooth it toward itself.
     */
    if (
        state.generation !=
        slot.aiResultGeneration
    )
    {
        const AIResult previousResult =
            state.result;

        const std::uint64_t previousSequence =
            state.sequence;

        state.generation =
            slot.aiResultGeneration;

        state.sequence =
            slot.aiResultFrameSequence;

        state.result =
            slot.aiResult;

        std::vector<VideoSlot::FenceDetectionState> currentFenceDetections;
        if (
            slot.hasVirtualFence &&
            state.result.inputWidth > 0 &&
            state.result.inputHeight > 0
        )
        {
            std::vector<bool> previousUsed(
                slot.previousFenceDetections.size(),
                false
            );

            for (const AIDetection& detection : state.result.detections)
            {
                if (detection.boundingBox.width <= 0.0f || detection.boundingBox.height <= 0.0f)
                    continue;

                const float centerX =
                    (detection.boundingBox.x + detection.boundingBox.width * 0.5f) /
                    static_cast<float>(state.result.inputWidth);
                const float centerY =
                    (detection.boundingBox.y + detection.boundingBox.height * 0.5f) /
                    static_cast<float>(state.result.inputHeight);
                const bool inside =
                    centerX >= slot.fenceX &&
                    centerX <= slot.fenceX + slot.fenceWidth &&
                    centerY >= slot.fenceY &&
                    centerY <= slot.fenceY + slot.fenceHeight;

                std::size_t bestIndex = slot.previousFenceDetections.size();
                float bestDistance = std::numeric_limits<float>::max();
                for (std::size_t i = 0; i < slot.previousFenceDetections.size(); ++i)
                {
                    const auto& previous = slot.previousFenceDetections[i];
                    if (previousUsed[i] || previous.classId != detection.classId)
                        continue;

                    const float dx = centerX - previous.centerX;
                    const float dy = centerY - previous.centerY;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < 0.15f && distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestIndex = i;
                    }
                }

                if (
                    inside &&
                    (bestIndex == slot.previousFenceDetections.size() ||
                     !slot.previousFenceDetections[bestIndex].inside)
                )
                {
                    queueAlert(
                        "VIRTUAL FENCE BREACH",
                        slot.sourceId,
                        IM_COL32(245, 45, 45, 255)
                    );
                }

                if (bestIndex < slot.previousFenceDetections.size())
                    previousUsed[bestIndex] = true;

                currentFenceDetections.push_back({
                    detection.classId,
                    centerX,
                    centerY,
                    inside
                });
            }
        }
        slot.previousFenceDetections = std::move(currentFenceDetections);

        std::vector<StableDetection> updated;
        updated.reserve(
            state.result.detections.size()
        );

        const float currentWidth =
            static_cast<float>(
                state.result.inputWidth
            );

        const float currentHeight =
            static_cast<float>(
                state.result.inputHeight
            );

        const float previousWidth =
            static_cast<float>(
                previousResult.inputWidth
            );

        const float previousHeight =
            static_cast<float>(
                previousResult.inputHeight
            );

        const bool havePrevious =
            !previousResult.detections.empty() &&
            previousWidth > 0.0f &&
            previousHeight > 0.0f &&
            previousSequence > 0 &&
            state.sequence > previousSequence;

        std::vector<bool> previousUsed(
            previousResult.detections.size(),
            false
        );

        auto centerX =
            [](const AIDetection& d, float w)
            {
                return (
                    d.boundingBox.x +
                    d.boundingBox.width * 0.5f
                ) / w;
            };

        auto centerY =
            [](const AIDetection& d, float h)
            {
                return (
                    d.boundingBox.y +
                    d.boundingBox.height * 0.5f
                ) / h;
            };

        for (
            const AIDetection& detection :
            state.result.detections
        )
        {
            if (
                (
                    detection.classId != AIObjectClass::Person &&
                    detection.classId != AIObjectClass::Vehicle &&
                    detection.classId != AIObjectClass::LicensePlate &&
                    !(slot.hasVirtualFence && detection.classId == AIObjectClass::Face)
                ) ||
                detection.boundingBox.width <= 0.0f ||
                detection.boundingBox.height <= 0.0f ||
                currentWidth <= 0.0f ||
                currentHeight <= 0.0f
            )
            {
                continue;
            }

            StableDetection stable;
            stable.classId =
                detection.classId;

            const float rawX =
                detection.boundingBox.x /
                currentWidth;

            const float rawY =
                detection.boundingBox.y /
                currentHeight;

            const float rawW =
                detection.boundingBox.width /
                currentWidth;

            const float rawH =
                detection.boundingBox.height /
                currentHeight;

            stable.x = rawX;
            stable.y = rawY;
            stable.width = rawW;
            stable.height = rawH;
            stable.stableFrames = 1;

            if (havePrevious)
            {
                const float currentCX =
                    centerX(detection, currentWidth);

                const float currentCY =
                    centerY(detection, currentHeight);

                float bestDistance =
                    std::numeric_limits<float>::max();

                std::size_t bestIndex =
                    previousResult.detections.size();

                for (
                    std::size_t i = 0;
                    i < previousResult.detections.size();
                    ++i
                )
                {
                    if (previousUsed[i])
                    {
                        continue;
                    }

                    const AIDetection& previous =
                        previousResult.detections[i];

                    if (
                        previous.classId !=
                        detection.classId ||
                        previous.boundingBox.width <= 0.0f ||
                        previous.boundingBox.height <= 0.0f
                    )
                    {
                        continue;
                    }

                    const float previousCX =
                        centerX(previous, previousWidth);

                    const float previousCY =
                        centerY(previous, previousHeight);

                    const float dx =
                        currentCX - previousCX;

                    const float dy =
                        currentCY - previousCY;

                    const float distance =
                        std::sqrt(
                            dx * dx +
                            dy * dy
                        );

                    if (
                        distance < 0.15f &&
                        distance < bestDistance
                    )
                    {
                        bestDistance = distance;
                        bestIndex = i;
                    }
                }

                if (
                    bestIndex <
                    previousResult.detections.size()
                )
                {
                    previousUsed[bestIndex] = true;

                    stable.stableFrames = 2;

                    for (
                        const StableDetection& previousStable :
                        state.stable
                    )
                    {
                        if (
                            previousStable.classId != detection.classId
                        )
                        {
                            continue;
                        }

                        const float stableDx =
                            currentCX -
                            (previousStable.x + previousStable.width * 0.5f);

                        const float stableDy =
                            currentCY -
                            (previousStable.y + previousStable.height * 0.5f);

                        if (
                            std::sqrt(
                                stableDx * stableDx +
                                stableDy * stableDy
                            ) < 0.15f
                        )
                        {
                            stable.stableFrames =
                                std::min<std::uint32_t>(
                                    previousStable.stableFrames + 1,
                                    2
                                );
                            break;
                        }
                    }

                    const AIDetection& previous =
                        previousResult.detections[bestIndex];

                    const float previousCX =
                        centerX(previous, previousWidth);

                    const float previousCY =
                        centerY(previous, previousHeight);

                    const float sequenceDelta =
                        static_cast<float>(
                            state.sequence -
                            previousSequence
                        );

                    if (sequenceDelta > 0.0f)
                    {
                        float vx =
                            (currentCX - previousCX) /
                            sequenceDelta;

                        float vy =
                            (currentCY - previousCY) /
                            sequenceDelta;

                        /*
                         * Reject detector noise as motion.  Real
                         * object movement is normally much smaller
                         * than this per source frame.
                         */
                        vx = std::clamp(vx, -0.035f, 0.035f);
                        vy = std::clamp(vy, -0.035f, 0.035f);

                        stable.vx = vx;
                        stable.vy = vy;
                    }
                }
            }

            /*
             * Carry the persistent dashboard identity into the overlay
             * state. The match is intentionally geometric and class-based
             * only; no new tracker or face-detector behavior is introduced.
             */
            float bestIdentityDistance =
                std::numeric_limits<float>::max();

            for (
                const VideoSlot::DashboardDetection& dashboard :
                slot.dashboardDetections
            )
            {
                if (
                    !dashboard.visible ||
                    dashboard.identityId <= 0 ||
                    dashboard.classId != detection.classId
                )
                {
                    continue;
                }

                const float dashboardCX =
                    dashboard.x + dashboard.width * 0.5f;
                const float dashboardCY =
                    dashboard.y + dashboard.height * 0.5f;

                const float distance =
                    std::sqrt(
                        (rawX + rawW * 0.5f - dashboardCX) *
                            (rawX + rawW * 0.5f - dashboardCX) +
                        (rawY + rawH * 0.5f - dashboardCY) *
                            (rawY + rawH * 0.5f - dashboardCY)
                    );

                if (distance < 0.18f && distance < bestIdentityDistance)
                {
                    bestIdentityDistance = distance;
                    stable.identityId = dashboard.identityId;
                    stable.identityType = dashboard.identityType;
                    stable.identityName = dashboard.identityName;
                }
            }

            stable.initialized = true;
            updated.push_back(stable);
        }

        /*
         * Smooth the newly measured positions against the previous
         * stable position.  This is deliberately moderate: enough to
         * remove detector jitter, but not enough to visibly drag the
         * box behind a moving object.
         */
        for (
            StableDetection& current :
            updated
        )
        {
            float bestDistance =
                std::numeric_limits<float>::max();

            const StableDetection* previousStable =
                nullptr;

            for (
                const StableDetection& previous :
                state.stable
            )
            {
                if (
                    previous.classId !=
                    current.classId
                )
                {
                    continue;
                }

                const float dx =
                    current.x -
                    previous.x;

                const float dy =
                    current.y -
                    previous.y;

                const float distance =
                    std::sqrt(
                        dx * dx +
                        dy * dy
                    );

                if (
                    distance < 0.18f &&
                    distance < bestDistance
                )
                {
                    bestDistance = distance;
                    previousStable = &previous;
                }
            }

            if (previousStable != nullptr)
            {
                constexpr float positionAlpha = 0.88f;

                current.x =
                    previousStable->x *
                    (1.0f - positionAlpha) +
                    current.x *
                    positionAlpha;

                current.y =
                    previousStable->y *
                    (1.0f - positionAlpha) +
                    current.y *
                    positionAlpha;
            }
        }

        state.stable =
            std::move(updated);
    }

    std::uint32_t sourceWidthValue = state.result.inputWidth;
    std::uint32_t sourceHeightValue = state.result.inputHeight;
    if (sourceWidthValue == 0 || sourceHeightValue == 0)
    {
        const auto texture = m_videoTextures.find(slot.sourceId);
        if (texture != m_videoTextures.end())
        {
            sourceWidthValue = texture->second.width;
            sourceHeightValue = texture->second.height;
        }
    }
    if (sourceWidthValue == 0 || sourceHeightValue == 0)
        return;

    const ImVec2 imageMin =
        ImGui::GetItemRectMin();

    const ImVec2 imageMax =
        ImGui::GetItemRectMax();

    const float displayWidth =
        imageMax.x -
        imageMin.x;

    const float displayHeight =
        imageMax.y -
        imageMin.y;

    if (
        displayWidth <= 0.0f ||
        displayHeight <= 0.0f
    )
    {
        return;
    }

    const float sourceWidth =
        static_cast<float>(sourceWidthValue);

    const float sourceHeight =
        static_cast<float>(sourceHeightValue);

    const float sourceAspect =
        sourceWidth /
        sourceHeight;

    const float displayAspect =
        displayWidth /
        displayHeight;

    float cropU = 0.0f;
    float cropV = 0.0f;
    float visibleU = 1.0f;
    float visibleV = 1.0f;

    if (
        sourceAspect >
        displayAspect
    )
    {
        visibleU =
            displayAspect /
            sourceAspect;

        cropU =
            (1.0f - visibleU) *
            0.5f;
    }
    else if (
        sourceAspect <
        displayAspect
    )
    {
        visibleV =
            sourceAspect /
            displayAspect;

        cropV =
            (1.0f - visibleV) *
            0.5f;
    }

    ImDrawList* drawList =
        ImGui::GetWindowDrawList();

    const auto screenToSource =
        [&](const ImVec2& point)
        {
            const float screenU = std::clamp((point.x - imageMin.x) / displayWidth, 0.0f, 1.0f);
            const float screenV = std::clamp((point.y - imageMin.y) / displayHeight, 0.0f, 1.0f);
            return ImVec2(cropU + screenU * visibleU, cropV + screenV * visibleV);
        };

    if (slot.fencePlacementMode)
    {
        const bool hovered = ImGui::IsMouseHoveringRect(imageMin, imageMax, true);
        if (!slot.fenceDrawing && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 start = screenToSource(ImGui::GetIO().MousePos);
            slot.fenceDrawing = true;
            slot.fenceStartX = start.x;
            slot.fenceStartY = start.y;
            slot.previewFenceX = start.x;
            slot.previewFenceY = start.y;
            slot.previewFenceWidth = 0.0f;
            slot.previewFenceHeight = 0.0f;
        }

        if (slot.fenceDrawing)
        {
            const ImVec2 current = screenToSource(ImGui::GetIO().MousePos);
            slot.previewFenceX = std::min(slot.fenceStartX, current.x);
            slot.previewFenceY = std::min(slot.fenceStartY, current.y);
            slot.previewFenceWidth = std::abs(current.x - slot.fenceStartX);
            slot.previewFenceHeight = std::abs(current.y - slot.fenceStartY);

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (slot.previewFenceWidth >= 0.01f && slot.previewFenceHeight >= 0.01f)
                {
                    slot.fenceX = slot.previewFenceX;
                    slot.fenceY = slot.previewFenceY;
                    slot.fenceWidth = slot.previewFenceWidth;
                    slot.fenceHeight = slot.previewFenceHeight;
                    slot.hasVirtualFence = true;
                    slot.previousFenceDetections.clear();
                    queueAlert(
                        "VIRTUAL FENCE SET",
                        slot.sourceId,
                        IM_COL32(235, 160, 45, 255)
                    );
                }
                slot.fenceDrawing = false;
                slot.fencePlacementMode = false;
            }
        }
    }

    const float visibleFenceX = slot.fenceDrawing ? slot.previewFenceX : slot.fenceX;
    const float visibleFenceY = slot.fenceDrawing ? slot.previewFenceY : slot.fenceY;
    const float visibleFenceWidth = slot.fenceDrawing ? slot.previewFenceWidth : slot.fenceWidth;
    const float visibleFenceHeight = slot.fenceDrawing ? slot.previewFenceHeight : slot.fenceHeight;
    if ((slot.hasVirtualFence || slot.fenceDrawing) && visibleFenceWidth > 0.0f && visibleFenceHeight > 0.0f)
    {
        const float left = std::max(visibleFenceX, cropU);
        const float top = std::max(visibleFenceY, cropV);
        const float right = std::min(visibleFenceX + visibleFenceWidth, cropU + visibleU);
        const float bottom = std::min(visibleFenceY + visibleFenceHeight, cropV + visibleV);
        if (right > left && bottom > top)
        {
            const ImVec2 fenceMin(
                imageMin.x + ((left - cropU) / visibleU) * displayWidth,
                imageMin.y + ((top - cropV) / visibleV) * displayHeight
            );
            const ImVec2 fenceMax(
                imageMin.x + ((right - cropU) / visibleU) * displayWidth,
                imageMin.y + ((bottom - cropV) / visibleV) * displayHeight
            );
            drawList->AddRectFilled(fenceMin, fenceMax, IM_COL32(245, 165, 45, 24));
            drawList->AddRect(fenceMin, fenceMax, IM_COL32(245, 165, 45, 230), 0.0f, 0, 2.0f);
        }
    }

    for (
        const StableDetection& stable :
        state.stable
    )
    {
        if (stable.stableFrames < 2)
        {
            continue;
        }

        const bool insideCurrentFence =
            slot.hasVirtualFence &&
            stable.x + stable.width * 0.5f >= slot.fenceX &&
            stable.x + stable.width * 0.5f <= slot.fenceX + slot.fenceWidth &&
            stable.y + stable.height * 0.5f >= slot.fenceY &&
            stable.y + stable.height * 0.5f <= slot.fenceY + slot.fenceHeight;

        if (stable.classId == AIObjectClass::Face && !insideCurrentFence)
            continue;

        float x =
            stable.x;
        float y =
            stable.y;
        float width =
            stable.width;
        float height =
            stable.height;

        /*
         * The AI result belongs to state.sequence, while the texture
         * on screen belongs to displayedFrameSequence.  Use the FULL
         * measured frame gap for position compensation.  A short cap
         * here leaves the box visibly behind whenever inference takes
         * more than a few frames, which is exactly the temporal lag we
         * are correcting.
         *
         * The velocity itself is already tightly bounded above, so this
         * does not create an unbounded jump.  We also never extrapolate
         * box width/height.
         */
        if (
            slot.displayedFrameSequence >
            state.sequence
        )
        {
            const std::uint64_t sequenceDelta =
                slot.displayedFrameSequence -
                state.sequence;

            constexpr std::uint64_t maximumPredictionFrames =
                24;

            const std::uint64_t predictionFrames =
                std::min<std::uint64_t>(
                    sequenceDelta,
                    maximumPredictionFrames
                );

            const float predictedX =
                stable.vx * static_cast<float>(predictionFrames);
            const float predictedY =
                stable.vy * static_cast<float>(predictionFrames);

            // Motion is estimated from detector boxes, not an object tracker.
            // Keep the projection inside a small fraction of this detection's
            // own size so a bad match cannot make its border jump elsewhere.
            const float maximumShiftX =
                std::max(0.005f, stable.width * 0.25f);
            const float maximumShiftY =
                std::max(0.005f, stable.height * 0.25f);

            x += std::clamp(predictedX, -maximumShiftX, maximumShiftX);
            y += std::clamp(predictedY, -maximumShiftY, maximumShiftY);
        }

        x = std::clamp(x, 0.0f, 1.0f);
        y = std::clamp(y, 0.0f, 1.0f);
        width = std::clamp(width, 0.0f, 1.0f - x);
        height = std::clamp(height, 0.0f, 1.0f - y);

        const float normalizedRight =
            x + width;

        const float normalizedBottom =
            y + height;

        if (
            normalizedRight <= cropU ||
            x >= cropU + visibleU ||
            normalizedBottom <= cropV ||
            y >= cropV + visibleV
        )
        {
            continue;
        }

        const float clippedLeft =
            std::max(x, cropU);

        const float clippedTop =
            std::max(y, cropV);

        const float clippedRight =
            std::min(normalizedRight, cropU + visibleU);

        const float clippedBottom =
            std::min(normalizedBottom, cropV + visibleV);

        const float screenLeft =
            imageMin.x +
            ((clippedLeft - cropU) / visibleU) *
            displayWidth;

        const float screenTop =
            imageMin.y +
            ((clippedTop - cropV) / visibleV) *
            displayHeight;

        const float screenRight =
            imageMin.x +
            ((clippedRight - cropU) / visibleU) *
            displayWidth;

        const float screenBottom =
            imageMin.y +
            ((clippedBottom - cropV) / visibleV) *
            displayHeight;

        if (
            screenRight <= screenLeft ||
            screenBottom <= screenTop
        )
        {
            continue;
        }

        ImU32 boxColor =
            insideCurrentFence
                ? IM_COL32(245, 35, 35, 255)
                : IM_COL32(32, 220, 230, 230);

        /*
         * Identity color is deliberately sourced from the persistent
         * history assignment. UNKNOWN remains cyan. Ally is green.
         * Criminal is red.
         *
         * Automatic future recognition will populate the same identity
         * fields when the face-recognition layer is connected; this
         * change does not alter the existing SCRFD detector.
         */
        if (
            insideCurrentFence
        )
        {
            boxColor = IM_COL32(245, 35, 35, 255);
        }
        else if (
            stable.identityId > 0 &&
            stable.identityType == DatabaseIdentityType::Ally
        )
        {
            boxColor = IM_COL32(40, 230, 90, 230);
        }
        else if (
            stable.identityId > 0 &&
            stable.identityType == DatabaseIdentityType::Criminal
        )
        {
            boxColor = IM_COL32(240, 50, 50, 230);
        }
        else if (
            stable.identityId > 0 &&
            stable.identityType == DatabaseIdentityType::Special
        )
        {
            boxColor = IM_COL32(185, 105, 245, 240);
        }

        drawList->AddRect(
            ImVec2(screenLeft, screenTop),
            ImVec2(screenRight, screenBottom),
            boxColor,
            0.0f,
            0,
            2.0f
        );

        char label[128] = {};

        /*
         * Find the corresponding current detection for the label and
         * confidence.  Geometry comes from the stable state above.
         */
        float confidence = 0.0f;

        float bestLabelDistance =
            std::numeric_limits<float>::max();

        for (
            const AIDetection& detection :
            state.result.detections
        )
        {
            if (
                detection.classId !=
                stable.classId
            )
            {
                continue;
            }

            const float cx =
                detection.boundingBox.x /
                sourceWidth +
                detection.boundingBox.width /
                sourceWidth *
                0.5f;

            const float cy =
                detection.boundingBox.y /
                sourceHeight +
                detection.boundingBox.height /
                sourceHeight *
                0.5f;

            const float stableCX =
                stable.x +
                stable.width *
                0.5f;

            const float stableCY =
                stable.y +
                stable.height *
                0.5f;

            const float dx = cx - stableCX;
            const float dy = cy - stableCY;
            const float distance =
                std::sqrt(dx * dx + dy * dy);

            if (distance < bestLabelDistance)
            {
                bestLabelDistance = distance;
                confidence = detection.confidence;
            }
        }

        std::snprintf(
            label,
            sizeof(label),
            "%s %.0f%%",
            aiObjectClassName(stable.classId),
            confidence * 100.0f
        );

        const ImVec2 labelSize =
            ImGui::CalcTextSize(label);

        const float labelX =
            screenLeft;

        const float labelY =
            std::max(
                imageMin.y,
                screenTop -
                labelSize.y -
                4.0f
            );

        drawList->AddRectFilled(
            ImVec2(labelX, labelY),
            ImVec2(
                labelX + labelSize.x + 8.0f,
                labelY + labelSize.y + 4.0f
            ),
            IM_COL32(5, 25, 30, 220)
        );

        drawList->AddText(
            ImVec2(labelX + 4.0f, labelY + 2.0f),
            IM_COL32(225, 250, 250, 255),
            label
        );
    }
}


void IBVAPUI::renderAIDetections(
    VideoSlot& slot
)
{
    /*
     * Detection rendering is performed by renderAIOverlay()
     * directly over the video image.
     *
     * Keep this function as a separate API boundary so the
     * UI can later support richer AI overlays without
     * changing the video-source architecture.
     */

    renderAIOverlay(
        slot
    );
}


void IBVAPUI::renderAIStatus(
    const VideoSlot& slot
)
{
    if (
        !m_aiInitialized
    )
    {
        ImGui::TextDisabled(
            "AI: unavailable"
        );

        return;
    }

    if (
        slot.aiError
    )
    {
        ImGui::TextDisabled(
            "AI: inference error"
        );

        return;
    }

    if (
        !slot.aiAvailable
    )
    {
        ImGui::TextDisabled(
            "AI: waiting"
        );

        return;
    }

    ImGui::TextColored(
        ImVec4(
            0.25f,
            0.85f,
            0.65f,
            1.00f
        ),
        "AI: active"
    );

    ImGui::TextDisabled(
        "Objects: %zu",
        slot.aiResult.detections.size()
    );
}


void IBVAPUI::shutdown()
{
    if (
        !m_initialized
    )
    {
        return;
    }

    /*
     * Shut down AI before destroying the video sources
     * and textures. This guarantees that the detector does
     * not outlive the UI-owned AI state.
     */
    shutdownAI();

    for (
        VideoSlot& slot :
        m_videoSlots
    )
    {
        clearDashboardTextures(slot);

        if (
            !slot.sourceId.empty()
        )
        {
            m_videoSourceManager.stopSource(
                slot.sourceId
            );

            m_videoSourceManager.removeSource(
                slot.sourceId
            );
        }
    }

    for (
        auto& pair :
        m_videoTextures
    )
    {
        if (
            pair.second.texture !=
            0
        )
        {
            glDeleteTextures(
                1,
                &pair.second.texture
            );

            pair.second.texture =
                0;
        }
    }

    m_videoTextures.clear();

    m_videoSlots.clear();

    clearHistoryTextures();
    m_historyRecords.clear();

    if (m_database != nullptr)
    {
        m_database->close();
        m_database.reset();
    }

    m_selectedHistoryId = 0;

    m_selectedSlot =
        -1;

    m_fullscreenSlot =
        -1;

    m_initialized =
        false;
}
// ==================== END PART 6 / 6 ====================
