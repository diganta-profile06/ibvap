#include "AIFrameEnhancement.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

AIFrame makeEnhancedAIFrame(
    const AIFrame& source,
    std::vector<std::uint8_t>& storage
)
{
    if (!source.valid() || source.width == 0 || source.height == 0)
        return source;

    std::size_t channels = 0;
    switch (source.format)
    {
        case PixelFormat::Gray8: channels = 1; break;
        case PixelFormat::RGB8:
        case PixelFormat::BGR8: channels = 3; break;
        case PixelFormat::RGBA8:
        case PixelFormat::BGRA8: channels = 4; break;
        default: return source;
    }

    const std::size_t width = source.width;
    const std::size_t height = source.height;
    if (source.stride < width * channels || height > SIZE_MAX / width / 3)
        return source;

    const std::size_t pixelCount = width * height;
    storage.resize(pixelCount * 3);
    double sum = 0.0;
    double chromaSum = 0.0;
    double gradientSum = 0.0;
    std::uint8_t minimum = 255;
    std::uint8_t maximum = 0;
    std::size_t samples = 0;

    for (std::size_t y = 0; y < height; y += 8)
    {
        const auto* row = source.data + y * source.stride;
        for (std::size_t x = 0; x < width; x += 8)
        {
            const std::size_t i = x * channels;
            const auto r = source.format == PixelFormat::Gray8 ? row[i] :
                row[i + (source.format == PixelFormat::BGR8 || source.format == PixelFormat::BGRA8 ? 2 : 0)];
            const auto g = source.format == PixelFormat::Gray8 ? row[i] : row[i + 1];
            const auto b = source.format == PixelFormat::Gray8 ? row[i] :
                row[i + (source.format == PixelFormat::BGR8 || source.format == PixelFormat::BGRA8 ? 0 : 2)];
            const auto luma = static_cast<std::uint8_t>(std::lround(0.299 * r + 0.587 * g + 0.114 * b));
            sum += luma;
            chromaSum += static_cast<double>(std::max({r, g, b}) - std::min({r, g, b}));
            minimum = std::min(minimum, luma);
            maximum = std::max(maximum, luma);
            ++samples;

            const auto lumaAt = [&](std::size_t sx, std::size_t sy)
            {
                const auto* sampleRow = source.data + sy * source.stride;
                const std::size_t si = sx * channels;
                const float sr = source.format == PixelFormat::Gray8 ? sampleRow[si] :
                    sampleRow[si + (source.format == PixelFormat::BGR8 || source.format == PixelFormat::BGRA8 ? 2 : 0)];
                const float sg = source.format == PixelFormat::Gray8 ? sampleRow[si] : sampleRow[si + 1];
                const float sb = source.format == PixelFormat::Gray8 ? sampleRow[si] :
                    sampleRow[si + (source.format == PixelFormat::BGR8 || source.format == PixelFormat::BGRA8 ? 0 : 2)];
                return 0.299f * sr + 0.587f * sg + 0.114f * sb;
            };
            if (x + 8 < width)
                gradientSum += std::abs(static_cast<double>(luma - lumaAt(x + 8, y)));
            if (y + 8 < height)
                gradientSum += std::abs(static_cast<double>(luma - lumaAt(x, y + 8)));
        }
    }

    const double mean = samples ? sum / samples : 128.0;
    const double meanChroma = samples ? chromaSum / samples : 255.0;
    const bool lowLight = mean < 72.0;
    const bool lowContrast = maximum - minimum < 48;
    const bool thermalLike = meanChroma < 12.0 && maximum - minimum < 160;
    const bool needsTone = lowLight || lowContrast || thermalLike;
    const double meanGradient = samples ? gradientSum / samples : 255.0;
    const bool needsMildSharpen = meanGradient < 7.5 && maximum - minimum > 18;
    if (!needsTone && !needsMildSharpen)
        return source;

    const float range = static_cast<float>(std::max(24, static_cast<int>(maximum) - static_cast<int>(minimum)));
    const float gamma = (lowLight || thermalLike) ? 0.72f : 0.88f;
    const float contrast = (lowContrast || thermalLike) ? 1.18f : 1.05f;

    for (std::size_t y = 0; y < height; ++y)
    {
        const auto* row = source.data + y * source.stride;
        for (std::size_t x = 0; x < width; ++x)
        {
            const std::size_t si = x * channels;
            const std::size_t di = (y * width + x) * 3;
            float r, g, b;
            if (source.format == PixelFormat::Gray8)
                r = g = b = row[si] / 255.0f;
            else
            {
                const bool bgr = source.format == PixelFormat::BGR8 || source.format == PixelFormat::BGRA8;
                r = row[si + (bgr ? 2 : 0)] / 255.0f;
                g = row[si + 1] / 255.0f;
                b = row[si + (bgr ? 0 : 2)] / 255.0f;
            }
            const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            float adjusted = luma;
            if (needsTone)
            {
                adjusted = std::clamp((luma * 255.0f - minimum) / range, 0.0f, 1.0f);
                adjusted = std::clamp((std::pow(adjusted, gamma) - 0.5f) * contrast + 0.5f, 0.0f, 1.0f);
            }
            const float scale = adjusted / std::max(0.001f, luma);
            storage[di] = static_cast<std::uint8_t>(std::lround(std::clamp(r * scale, 0.0f, 1.0f) * 255.0f));
            storage[di + 1] = static_cast<std::uint8_t>(std::lround(std::clamp(g * scale, 0.0f, 1.0f) * 255.0f));
            storage[di + 2] = static_cast<std::uint8_t>(std::lround(std::clamp(b * scale, 0.0f, 1.0f) * 255.0f));
        }
    }

    if (needsMildSharpen && width > 2 && height > 2)
    {
        const std::vector<std::uint8_t> original = storage;
        for (std::size_t y = 1; y + 1 < height; ++y)
        {
            for (std::size_t x = 1; x + 1 < width; ++x)
            {
                const std::size_t i = (y * width + x) * 3;
                const std::size_t neighbors[] = {
                    (y * width + x - 1) * 3,
                    (y * width + x + 1) * 3,
                    ((y - 1) * width + x) * 3,
                    ((y + 1) * width + x) * 3
                };
                for (std::size_t c = 0; c < 3; ++c)
                {
                    const float adjacent = (original[neighbors[0] + c] + original[neighbors[1] + c] +
                        original[neighbors[2] + c] + original[neighbors[3] + c]) * 0.25f;
                    const float center = original[i + c];
                    storage[i + c] = static_cast<std::uint8_t>(std::clamp(center + (center - adjacent) * 0.18f, 0.0f, 255.0f));
                }
            }
        }
    }

    AIFrame result;
    result.data = storage.data();
    result.width = source.width;
    result.height = source.height;
    result.stride = source.width * 3;
    result.format = PixelFormat::RGB8;
    return result;
}
