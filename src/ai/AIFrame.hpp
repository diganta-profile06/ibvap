#pragma once

#include "../video/Frame.hpp"

#include <cstdint>

struct AIFrame
{
    const std::uint8_t* data = nullptr;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;

    PixelFormat format = PixelFormat::Unknown;

    bool valid() const noexcept
    {
        return
            data != nullptr &&
            width > 0 &&
            height > 0 &&
            stride > 0 &&
            format != PixelFormat::Unknown;
    }

    static AIFrame fromFrame(
        const Frame& frame
    ) noexcept
    {
        AIFrame result;

        if (!frame.valid())
        {
            return result;
        }

        result.data =
            frame.data();

        result.width =
            frame.width;

        result.height =
            frame.height;

        result.stride =
            frame.stride;

        result.format =
            frame.format;

        return result;
    }
};