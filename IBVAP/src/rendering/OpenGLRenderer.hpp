#pragma once

#include "Renderer.hpp"

#include <SDL3/SDL.h>


class OpenGLRenderer final : public Renderer
{
public:
    explicit OpenGLRenderer(SDL_Window* window);

    ~OpenGLRenderer() override;

    OpenGLRenderer(const OpenGLRenderer&) = delete;
    OpenGLRenderer& operator=(const OpenGLRenderer&) = delete;

    // Configure attributes before the SDL window is created.
    static void configureContext();

    bool initialize() override;

    void beginFrame() override;

    void render() override;

    void shutdown() override;

private:
    SDL_Window* m_window = nullptr;

    SDL_GLContext m_gl_context = nullptr;

    bool m_initialized = false;
};