#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>


// =============================================================
// PixelFormat
// =============================================================
//
// Describes how raw CPU-side video pixels are stored.
//
// We keep this independent of OpenCV, SDL, Vulkan and OpenGL.
// Those systems can convert/consume the frame later.
//
// =============================================================

enum class PixelFormat
{
    Unknown,

    // 8-bit RGB:
    // R G B R G B ...
    RGB8,

    // 8-bit RGBA:
    // R G B A R G B A ...
    RGBA8,

    // 8-bit BGR:
    // B G R B G R ...
    BGR8,

    // 8-bit BGRA:
    // B G R A B G R A ...
    BGRA8,

    // 8-bit monochrome.
    Gray8
};


// =============================================================
// FrameBuffer
// =============================================================
//
// The actual CPU-side memory containing pixel data.
//
// A shared buffer allows different parts of the pipeline to
// reference the same frame without immediately copying it.
//
// Example:
//
// Decoder
//    │
//    ▼
// FrameBuffer
//    │
//    ├── Video renderer
//    │
//    └── Future AI pipeline
//
// =============================================================

struct FrameBuffer
{
    std::vector<std::uint8_t> data;

    std::size_t size() const noexcept
    {
        return data.size();
    }

    bool empty() const noexcept
    {
        return data.empty();
    }
};


// =============================================================
// Frame
// =============================================================
//
// Represents one decoded video frame.
//
// This is the common frame representation that every source
// type will eventually provide.
//
// =============================================================

struct Frame
{
    // ---------------------------------------------------------
    // Pixel data
    // ---------------------------------------------------------

    std::shared_ptr<FrameBuffer> buffer;


    // ---------------------------------------------------------
    // Frame dimensions
    // ---------------------------------------------------------

    std::uint32_t width = 0;

    std::uint32_t height = 0;


    // ---------------------------------------------------------
    // Bytes per row
    //
    // This is called the stride/pitch in many graphics APIs.
    // It allows rows to contain padding when necessary.
    // ---------------------------------------------------------

    std::uint32_t stride = 0;


    // ---------------------------------------------------------
    // Pixel format
    // ---------------------------------------------------------

    PixelFormat format =
        PixelFormat::Unknown;


    // ---------------------------------------------------------
    // Presentation timestamp
    //
    // Time position of this frame within its source stream,
    // expressed in microseconds.
    //
    // Using an integer avoids floating-point timing errors.
    // ---------------------------------------------------------

    std::int64_t timestampUs = 0;


    // ---------------------------------------------------------
    // Frame sequence number
    //
    // Monotonically increasing number assigned by the source.
    // Useful for detecting dropped frames.
    // ---------------------------------------------------------

    std::uint64_t sequence = 0;


    // ---------------------------------------------------------
    // Validity
    // ---------------------------------------------------------

    bool valid() const noexcept
    {
        return
            buffer != nullptr &&
            !buffer->empty() &&
            width > 0 &&
            height > 0 &&
            stride > 0 &&
            format != PixelFormat::Unknown;
    }


    // ---------------------------------------------------------
    // Raw data access
    // ---------------------------------------------------------

    const std::uint8_t* data() const noexcept
    {
        if (!buffer)
        {
            return nullptr;
        }

        return buffer->data.data();
    }


    std::uint8_t* data() noexcept
    {
        if (!buffer)
        {
            return nullptr;
        }

        return buffer->data.data();
    }


    // ---------------------------------------------------------
    // Total number of bytes
    // ---------------------------------------------------------

    std::size_t byteSize() const noexcept
    {
        if (!buffer)
        {
            return 0;
        }

        return buffer->size();
    }
};