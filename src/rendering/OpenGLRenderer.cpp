#include "OpenGLRenderer.hpp"

#include <SDL3/SDL_opengl.h>

#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

OpenGLRenderer::OpenGLRenderer(SDL_Window* window)
    : m_window(window)
{
}

OpenGLRenderer::~OpenGLRenderer()
{
    shutdown();
}

void OpenGLRenderer::configureContext()
{
#ifdef __EMSCRIPTEN__

    // ---------------------------------------------------------
    // WebAssembly / Browser
    // ---------------------------------------------------------
    //
    // SDL3 maps the OpenGL API to WebGL/OpenGL ES when
    // compiling through Emscripten.
    //
    // Do NOT request the native desktop OpenGL 3.3 core
    // profile here.
    //

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MAJOR_VERSION,
        3
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MINOR_VERSION,
        0
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_ES
    );

#else

    // ---------------------------------------------------------
    // Native Windows / Linux / macOS
    // ---------------------------------------------------------
    //
    // Keep the existing native OpenGL configuration unchanged.
    //

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MAJOR_VERSION,
        3
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MINOR_VERSION,
        3
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE
    );

#endif

    // ---------------------------------------------------------
    // Framebuffer configuration
    // ---------------------------------------------------------

    SDL_GL_SetAttribute(
        SDL_GL_DOUBLEBUFFER,
        1
    );

    SDL_GL_SetAttribute(
        SDL_GL_DEPTH_SIZE,
        24
    );

    SDL_GL_SetAttribute(
        SDL_GL_STENCIL_SIZE,
        8
    );
}

bool OpenGLRenderer::initialize()
{
    if (m_window == nullptr)
    {
        std::cerr
            << "OpenGLRenderer: Invalid SDL window.\n";

        return false;
    }

    // ---------------------------------------------------------
    // Create OpenGL / WebGL context
    // ---------------------------------------------------------

    m_gl_context =
        SDL_GL_CreateContext(m_window);

    if (m_gl_context == nullptr)
    {
        std::cerr
            << "OpenGL context creation failed: "
            << SDL_GetError()
            << '\n';

        return false;
    }

    // ---------------------------------------------------------
    // Make context current
    // ---------------------------------------------------------

    if (!SDL_GL_MakeCurrent(
            m_window,
            m_gl_context))
    {
        std::cerr
            << "Failed to make OpenGL context current: "
            << SDL_GetError()
            << '\n';

        SDL_GL_DestroyContext(
            m_gl_context
        );

        m_gl_context = nullptr;

        return false;
    }

    // ---------------------------------------------------------
    // Enable VSync
    // ---------------------------------------------------------

    if (!SDL_GL_SetSwapInterval(1))
    {
        std::cerr
            << "Warning: VSync could not be enabled: "
            << SDL_GetError()
            << '\n';
    }

    m_initialized = true;

    std::cout
        << "OpenGL renderer initialized successfully.\n";

#ifdef __EMSCRIPTEN__

    std::cout
        << "IBVAP WebAssembly WebGL renderer active.\n";

#else

    std::cout
        << "IBVAP native OpenGL renderer active.\n";

#endif

    return true;
}

void OpenGLRenderer::beginFrame()
{
    if (!m_initialized)
    {
        return;
    }

    // ---------------------------------------------------------
    // Get framebuffer size
    // ---------------------------------------------------------

    int framebuffer_width = 0;
    int framebuffer_height = 0;

    SDL_GetWindowSizeInPixels(
        m_window,
        &framebuffer_width,
        &framebuffer_height
    );

    // ---------------------------------------------------------
    // Configure viewport
    // ---------------------------------------------------------

    glViewport(
        0,
        0,
        framebuffer_width,
        framebuffer_height
    );

    // ---------------------------------------------------------
    // Clear framebuffer
    // ---------------------------------------------------------

    glClearColor(
        0.018f,
        0.025f,
        0.028f,
        1.0f
    );

    glClear(
        GL_COLOR_BUFFER_BIT
    );
}

void OpenGLRenderer::render()
{
    if (!m_initialized)
    {
        return;
    }

    SDL_GL_SwapWindow(
        m_window
    );
}

void OpenGLRenderer::shutdown()
{
    if (m_gl_context != nullptr)
    {
        SDL_GL_DestroyContext(
            m_gl_context
        );

        m_gl_context = nullptr;
    }

    m_initialized = false;
}