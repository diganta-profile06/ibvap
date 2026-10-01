#pragma once

#include "../AIFrame.hpp"
#include "../AIResult.hpp"

#include <memory>
#include <string>
#include <vector>

struct LicensePlateDetection
{
    AIBoundingBox boundingBox;

    float confidence = 0.0f;
};

class LicensePlateDetector
{
public:
    LicensePlateDetector();
    ~LicensePlateDetector();

    LicensePlateDetector(
        const LicensePlateDetector&
    ) = delete;

    LicensePlateDetector& operator=(
        const LicensePlateDetector&
    ) = delete;

    bool initialize(
        const std::string& paramPath,
        const std::string& binPath
    );

    void shutdown() noexcept;

    bool isInitialized() const noexcept;

    bool detect(
        const AIFrame& frame,
        std::vector<LicensePlateDetection>& plates
    );

    bool detect(
        const AIFrame& frame,
        AIResult& result
    );

private:
    struct Impl;

    std::unique_ptr<Impl> m_impl;
};