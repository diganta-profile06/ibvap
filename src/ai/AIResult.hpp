#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class AIObjectClass
{
    Unknown,

    Person,

    Vehicle,

    Weapon,

    Face,

    LicensePlate
};

struct AIBoundingBox
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct AIDetection
{
    AIObjectClass classId =
        AIObjectClass::Unknown;

    float confidence = 0.0f;

    AIBoundingBox boundingBox;

    std::int64_t trackId = -1;

    std::string identity;
};

struct AIResult
{
    std::vector<AIDetection> detections;

    std::uint32_t inputWidth = 0;
    std::uint32_t inputHeight = 0;

    std::int64_t timestampUs = 0;

    bool inferenceSucceeded = false;

    void clear()
    {
        detections.clear();

        inputWidth = 0;
        inputHeight = 0;

        timestampUs = 0;

        inferenceSucceeded = false;
    }
};