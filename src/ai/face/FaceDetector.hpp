#pragma once

#include "../AIFrame.hpp"
#include "../AIResult.hpp"

#include <memory>
#include <string>
#include <vector>

struct FaceDetection
{
    AIBoundingBox boundingBox;

    float confidence = 0.0f;

    // SCRFD facial landmarks:
    // 0 = left eye
    // 1 = right eye
    // 2 = nose
    // 3 = left mouth corner
    // 4 = right mouth corner
    float landmarks[5][2] = {};

    bool hasLandmarks = false;
};

class FaceDetector
{
public:
    FaceDetector();
    ~FaceDetector();

    FaceDetector(const FaceDetector&) = delete;
    FaceDetector& operator=(const FaceDetector&) = delete;

    bool initialize(
        const std::string& paramPath,
        const std::string& binPath
    );

    void shutdown() noexcept;

    bool isInitialized() const noexcept;

    bool detect(
        const AIFrame& frame,
        std::vector<FaceDetection>& faces
    );

    bool detect(
        const AIFrame& frame,
        AIResult& result
    );

private:
    struct Impl;

    std::unique_ptr<Impl> m_impl;
};