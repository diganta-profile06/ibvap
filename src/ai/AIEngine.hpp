#pragma once

#include "AIFrame.hpp"
#include "AIResult.hpp"

#include <memory>
#include <string>

class AIEngine
{
public:
    AIEngine();
    ~AIEngine();

    AIEngine(const AIEngine&) = delete;
    AIEngine& operator=(const AIEngine&) = delete;

    bool initialize(
        const std::string& paramPath,
        const std::string& binPath
    );

    void shutdown() noexcept;

    bool isInitialized() const noexcept;

    bool infer(
        const AIFrame& frame,
        AIResult& result
    );

private:
    class Impl;

    std::unique_ptr<Impl> m_impl;
};