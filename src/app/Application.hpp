#pragma once

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__

#include "../../web/video/WebVideoSourceManager.hpp"

using IBVAPVideoSourceManager =
    WebVideoSourceManager;

#else

#include "../video/VideoSourceManager.hpp"

using IBVAPVideoSourceManager =
    VideoSourceManager;

#endif

#include <memory>

class Renderer;
class IBVAPUI;

class Application final
{
public:
    Application();

    ~Application();

    Application(
        const Application&
    ) = delete;

    Application& operator=(
        const Application&
    ) = delete;

    bool initialize();

    void run();

    void shutdown();

private:
    void processEvents();

    void beginFrame();

    void renderFrame();

private:
    bool m_running = false;

    SDL_Window* m_window = nullptr;

    std::unique_ptr<Renderer>
        m_renderer;

    IBVAPVideoSourceManager
        m_videoSourceManager;

    std::unique_ptr<IBVAPUI>
        m_ui;
};