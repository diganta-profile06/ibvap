#pragma once

#include "../AIFrame.hpp"
#include "../AIResult.hpp"

#include <string>
#include <vector>

class ObjectDetector
{
public:
    ObjectDetector();
    ~ObjectDetector();

    ObjectDetector(const ObjectDetector&) = delete;
    ObjectDetector& operator=(const ObjectDetector&) = delete;

    bool initialize(
        const std::string& paramPath,
        const std::string& binPath
    );

    void shutdown() noexcept;

    bool isInitialized() const noexcept;

    bool detect(
        const AIFrame& frame,
        AIResult& result
    );

private:
    struct Impl;

    Impl* m_impl = nullptr;
};