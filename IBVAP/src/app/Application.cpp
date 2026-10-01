#include "Application.hpp"

#include "../rendering/Renderer.hpp"
#include "../rendering/OpenGLRenderer.hpp"
#include "../ui/UI.hpp"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"

#include <iostream>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

Application::Application()
{
}

Application::~Application()
{
    shutdown();
}

bool Application::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr
            << "SDL3 initialization failed: "
            << SDL_GetError()
            << '\n';

        return false;
    }

    OpenGLRenderer::configureContext();

    m_window = SDL_CreateWindow(
        "IBVAP - AI Border Surveillance System",
        1280,
        720,
        SDL_WINDOW_OPENGL |
        SDL_WINDOW_RESIZABLE
    );

    if (m_window == nullptr)
    {
        std::cerr
            << "SDL window creation failed: "
            << SDL_GetError()
            << '\n';

        SDL_Quit();

        return false;
    }

    m_renderer =
        std::make_unique<OpenGLRenderer>(
            m_window
        );

    if (!m_renderer->initialize())
    {
        std::cerr
            << "Renderer initialization failed.\n";

        m_renderer.reset();

        SDL_DestroyWindow(
            m_window
        );

        m_window = nullptr;

        SDL_Quit();

        return false;
    }

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io =
        ImGui::GetIO();

    (void)io;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForOpenGL(
            m_window,
            nullptr))
    {
        std::cerr
            << "Dear ImGui SDL3 backend "
               "initialization failed.\n";

        ImGui::DestroyContext();

        m_renderer.reset();

        SDL_DestroyWindow(
            m_window
        );

        m_window = nullptr;

        SDL_Quit();

        return false;
    }

#ifdef __EMSCRIPTEN__

    /*
     * ---------------------------------------------------------
     * WebAssembly / WebGL
     * ---------------------------------------------------------
     *
     * Emscripten provides the WebGL/OpenGL ES context.
     * Dear ImGui's OpenGL3 backend therefore uses GLSL ES.
     */

    if (!ImGui_ImplOpenGL3_Init(
            "#version 300 es"))
    {
        std::cerr
            << "Dear ImGui OpenGL ES backend "
               "initialization failed.\n";

        ImGui_ImplSDL3_Shutdown();

        ImGui::DestroyContext();

        m_renderer.reset();

        SDL_DestroyWindow(
            m_window
        );

        m_window = nullptr;

        SDL_Quit();

        return false;
    }

#else

    /*
     * ---------------------------------------------------------
     * Native OpenGL
     * ---------------------------------------------------------
     *
     * Keep the existing native OpenGL 3.3 path.
     */

    if (!ImGui_ImplOpenGL3_Init(
            "#version 330"))
    {
        std::cerr
            << "Dear ImGui OpenGL backend "
               "initialization failed.\n";

        ImGui_ImplSDL3_Shutdown();

        ImGui::DestroyContext();

        m_renderer.reset();

        SDL_DestroyWindow(
            m_window
        );

        m_window = nullptr;

        SDL_Quit();

        return false;
    }

#endif

    /*
     * ---------------------------------------------------------
     * UI
     * ---------------------------------------------------------
     *
     * The UI class is named IBVAPUI instead of UI because
     * OpenSSL exposes a type named UI through Crow/Asio.
     *
     * This avoids the OpenSSL/UI name collision.
     */

    m_ui =
        std::make_unique<IBVAPUI>(
            m_window,
            m_videoSourceManager
        );

    m_ui->initialize();

    m_running = true;

    std::cout
        << "IBVAP application initialized successfully.\n";

    return true;
}

void Application::run()
{
#ifdef __EMSCRIPTEN__

    /*
     * ---------------------------------------------------------
     * WebAssembly main loop
     * ---------------------------------------------------------
     *
     * The browser owns the main event loop.
     *
     * Do not use:
     *
     *     while (m_running)
     *
     * here.
     *
     * Emscripten calls mainLoopIteration() once per browser
     * frame and returns control to the browser afterwards.
     */

    emscripten_set_main_loop_arg(
        [](void* userdata)
        {
            auto* application =
                static_cast<Application*>(userdata);

            application->processEvents();

            if (!application->m_running)
            {
                emscripten_cancel_main_loop();

                return;
            }

            application->beginFrame();

            application->m_ui->render();

            application->renderFrame();
        },
        this,
        0,
        true
    );

#else

    /*
     * ---------------------------------------------------------
     * Native application loop
     * ---------------------------------------------------------
     *
     * Keep the existing native behavior.
     */

    while (m_running)
    {
        processEvents();

        beginFrame();

        m_ui->render();

        renderFrame();
    }

#endif
}

void Application::processEvents()
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL3_ProcessEvent(
            &event
        );

        if (event.type ==
            SDL_EVENT_QUIT)
        {
            m_running = false;
        }

        if (event.type ==
            SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            if (
                m_window != nullptr &&
                event.window.windowID ==
                    SDL_GetWindowID(
                        m_window
                    )
            )
            {
                m_running = false;
            }
        }
    }
}

void Application::beginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();

    m_renderer->beginFrame();
}

void Application::renderFrame()
{
    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(
        ImGui::GetDrawData()
    );

    m_renderer->render();
}

void Application::shutdown()
{
#ifdef __EMSCRIPTEN__

    /*
     * If shutdown occurs while the browser main loop is active,
     * stop the Emscripten callback first.
     */

    if (!m_running)
    {
        emscripten_cancel_main_loop();
    }

#endif

    if (
        !m_running &&
        m_window == nullptr &&
        m_renderer == nullptr &&
        m_ui == nullptr
    )
    {
        return;
    }

    m_running = false;

    /*
     * Stop UI first.
     */
    if (m_ui != nullptr)
    {
        m_ui->shutdown();

        m_ui.reset();
    }

    /*
     * Stop all video sources.
     */
    m_videoSourceManager.stopAll();

    /*
     * Shutdown Dear ImGui.
     */
    if (
        ImGui::GetCurrentContext() !=
        nullptr
    )
    {
        ImGui_ImplOpenGL3_Shutdown();

        ImGui_ImplSDL3_Shutdown();

        ImGui::DestroyContext();
    }

    /*
     * Shutdown renderer.
     */
    if (m_renderer != nullptr)
    {
        m_renderer->shutdown();

        m_renderer.reset();
    }

    /*
     * Destroy SDL window.
     */
    if (m_window != nullptr)
    {
        SDL_DestroyWindow(
            m_window
        );

        m_window = nullptr;
    }

    SDL_Quit();

    std::cout
        << "IBVAP shutdown successfully.\n";
}