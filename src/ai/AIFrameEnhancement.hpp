#pragma once

#include "AIFrame.hpp"

#include <cstdint>
#include <vector>

// Produces an enhanced RGB8 working frame when the input looks unusually
// dark or low contrast. The source frame is never modified. The implementation
// uses only the C++ standard library so it is shared by Windows and Emscripten.
AIFrame makeEnhancedAIFrame(
    const AIFrame& source,
    std::vector<std::uint8_t>& storage
);
