#pragma once

class Renderer
{
public:
    virtual ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    virtual bool initialize() = 0;

    virtual void beginFrame() = 0;

    virtual void render() = 0;

    virtual void shutdown() = 0;

protected:
    Renderer() = default;
};