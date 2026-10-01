#include "objectdetector.hpp"

#include <ncnn/net.h>
#include <ncnn/mat.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr int INPUT_SIZE =
    640;

constexpr int NUM_CLASSES =
    80;

constexpr int REGRESSION_BINS =
    16;

constexpr int REGRESSION_CHANNELS =
    REGRESSION_BINS * 4;

constexpr int OUTPUT_VALUES_PER_DETECTION =
    REGRESSION_CHANNELS +
    NUM_CLASSES;

constexpr int TOTAL_PREDICTIONS =
    8400;

/*
 * Keep this low enough to preserve:
 *
 *     - distant people
 *     - moderately blurry people
 *     - partially visible people
 *
 * We do NOT solve false positives by simply raising this
 * threshold to 0.6 or 0.7.
 */
constexpr float DEFAULT_CONFIDENCE_THRESHOLD =
    0.25f;

constexpr float DEFAULT_NMS_THRESHOLD =
    0.45f;

constexpr int MAX_DETECTIONS =
    300;


/*
 * Person-specific false-positive protection.
 *
 * A real person can be:
 *
 *     - standing
 *     - sitting
 *     - crouching
 *     - partially visible
 *     - near an image boundary
 *     - distant
 *
 * Therefore these limits are deliberately conservative.
 *
 * The main thing we reject is an extremely wide and shallow
 * region being classified as a person.
 *
 * Example:
 *
 *     width  = 380
 *     height =  75
 *
 *     ratio ~= 5.0
 *
 * That is a strong candidate for the type of false person
 * detection visible in the current test video.
 */
constexpr float PERSON_MAX_ASPECT_RATIO =
    3.50f;


/*
 * Very small detections are NOT rejected aggressively.
 *
 * This exists only as a protection against numerical/
 * pathological boxes. Distant objects must remain possible.
 */
constexpr float MIN_NORMALIZED_BOX_AREA =
    0.00005f;


/*
 * A low-confidence person detection receives the stricter
 * shape check.
 *
 * This lets a genuine distant person around 40% survive if
 * the box has a plausible person shape, while suppressing
 * very wide low-confidence false positives.
 */
constexpr float LOW_CONFIDENCE_PERSON_THRESHOLD =
    0.60f;

constexpr float LOW_CONFIDENCE_PERSON_MAX_ASPECT_RATIO =
    2.75f;


struct Candidate
{
    int classId =
        -1;

    float confidence =
        0.0f;

    float x1 =
        0.0f;

    float y1 =
        0.0f;

    float x2 =
        0.0f;

    float y2 =
        0.0f;
};


float sigmoid(
    float value
)
{
    if (
        value >=
        0.0f
    )
    {
        const float z =
            std::exp(
                -value
            );

        return
            1.0f /
            (
                1.0f +
                z
            );
    }

    const float z =
        std::exp(
            value
        );

    return
        z /
        (
            1.0f +
            z
        );
}


float intersectionOverUnion(
    const Candidate& a,
    const Candidate& b
)
{
    const float x1 =
        std::max(
            a.x1,
            b.x1
        );

    const float y1 =
        std::max(
            a.y1,
            b.y1
        );

    const float x2 =
        std::min(
            a.x2,
            b.x2
        );

    const float y2 =
        std::min(
            a.y2,
            b.y2
        );

    const float width =
        std::max(
            0.0f,
            x2 - x1
        );

    const float height =
        std::max(
            0.0f,
            y2 - y1
        );

    const float intersection =
        width *
        height;

    const float areaA =
        std::max(
            0.0f,
            a.x2 - a.x1
        ) *
        std::max(
            0.0f,
            a.y2 - a.y1
        );

    const float areaB =
        std::max(
            0.0f,
            b.x2 - b.x1
        ) *
        std::max(
            0.0f,
            b.y2 - b.y1
        );

    const float unionArea =
        areaA +
        areaB -
        intersection;

    if (
        unionArea <=
        0.0f
    )
    {
        return 0.0f;
    }

    return
        intersection /
        unionArea;
}


void softmax16(
    const float* values,
    float* probabilities
)
{
    float maximum =
        values[0];

    for (
        int i = 1;
        i < REGRESSION_BINS;
        ++i
    )
    {
        maximum =
            std::max(
                maximum,
                values[i]
            );
    }

    float sum =
        0.0f;

    for (
        int i = 0;
        i < REGRESSION_BINS;
        ++i
    )
    {
        probabilities[i] =
            std::exp(
                values[i] -
                maximum
            );

        sum +=
            probabilities[i];
    }

    if (
        sum <=
        0.0f
    )
    {
        for (
            int i = 0;
            i < REGRESSION_BINS;
            ++i
        )
        {
            probabilities[i] =
                0.0f;
        }

        return;
    }

    const float inverse =
        1.0f /
        sum;

    for (
        int i = 0;
        i < REGRESSION_BINS;
        ++i
    )
    {
        probabilities[i] *=
            inverse;
    }
}


float decodeDFL(
    const float* values
)
{
    float probabilities[
        REGRESSION_BINS
    ];

    softmax16(
        values,
        probabilities
    );

    float result =
        0.0f;

    for (
        int i = 0;
        i < REGRESSION_BINS;
        ++i
    )
    {
        result +=
            probabilities[i] *
            static_cast<float>(
                i
            );
    }

    return result;
}


void nmsPerClass(
    std::vector<Candidate>& candidates,
    float threshold
)
{
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const Candidate& a,
            const Candidate& b
        )
        {
            return
                a.confidence >
                b.confidence;
        }
    );

    std::vector<bool>
        suppressed(
            candidates.size(),
            false
        );

    std::vector<Candidate>
        result;

    result.reserve(
        std::min(
            candidates.size(),
            static_cast<std::size_t>(
                MAX_DETECTIONS
            )
        )
    );

    for (
        std::size_t i = 0;
        i < candidates.size();
        ++i
    )
    {
        if (
            suppressed[i]
        )
        {
            continue;
        }

        result.push_back(
            candidates[i]
        );

        if (
            result.size() >=
            static_cast<std::size_t>(
                MAX_DETECTIONS
            )
        )
        {
            break;
        }

        for (
            std::size_t j =
                i + 1;
            j < candidates.size();
            ++j
        )
        {
            if (
                suppressed[j]
            )
            {
                continue;
            }

            if (
                candidates[i].classId !=
                candidates[j].classId
            )
            {
                continue;
            }

            if (
                intersectionOverUnion(
                    candidates[i],
                    candidates[j]
                ) >
                threshold
            )
            {
                suppressed[j] =
                    true;
            }
        }
    }

    candidates =
        std::move(
            result
        );
}


bool convertFrameToRGB(
    const AIFrame& frame,
    std::vector<std::uint8_t>& rgb
)
{
    if (
        !frame.valid()
    )
    {
        return false;
    }

    const std::size_t width =
        static_cast<std::size_t>(
            frame.width
        );

    const std::size_t height =
        static_cast<std::size_t>(
            frame.height
        );

    std::size_t sourceBytesPerPixel =
        0;

    switch (
        frame.format
    )
    {
        case PixelFormat::RGB8:
        case PixelFormat::BGR8:

            sourceBytesPerPixel =
                3;

            break;

        case PixelFormat::RGBA8:
        case PixelFormat::BGRA8:

            sourceBytesPerPixel =
                4;

            break;

        case PixelFormat::Gray8:

            sourceBytesPerPixel =
                1;

            break;

        default:

            return false;
    }

    const std::size_t minimumStride =
        width *
        sourceBytesPerPixel;

    if (
        static_cast<std::size_t>(
            frame.stride
        ) <
        minimumStride
    )
    {
        return false;
    }

    rgb.resize(
        width *
        height *
        3
    );

    for (
        std::size_t y = 0;
        y < height;
        ++y
    )
    {
        const std::uint8_t*
            sourceRow =
                frame.data +
                y *
                static_cast<std::size_t>(
                    frame.stride
                );

        std::uint8_t*
            destinationRow =
                rgb.data() +
                y *
                width *
                3;

        for (
            std::size_t x = 0;
            x < width;
            ++x
        )
        {
            const std::size_t
                destination =
                    x * 3;

            switch (
                frame.format
            )
            {
                case PixelFormat::RGB8:
                {
                    const std::size_t
                        source =
                            x * 3;

                    destinationRow[
                        destination + 0
                    ] =
                        sourceRow[
                            source + 0
                        ];

                    destinationRow[
                        destination + 1
                    ] =
                        sourceRow[
                            source + 1
                        ];

                    destinationRow[
                        destination + 2
                    ] =
                        sourceRow[
                            source + 2
                        ];

                    break;
                }

                case PixelFormat::BGR8:
                {
                    const std::size_t
                        source =
                            x * 3;

                    destinationRow[
                        destination + 0
                    ] =
                        sourceRow[
                            source + 2
                        ];

                    destinationRow[
                        destination + 1
                    ] =
                        sourceRow[
                            source + 1
                        ];

                    destinationRow[
                        destination + 2
                    ] =
                        sourceRow[
                            source + 0
                        ];

                    break;
                }

                case PixelFormat::RGBA8:
                {
                    const std::size_t
                        source =
                            x * 4;

                    destinationRow[
                        destination + 0
                    ] =
                        sourceRow[
                            source + 0
                        ];

                    destinationRow[
                        destination + 1
                    ] =
                        sourceRow[
                            source + 1
                        ];

                    destinationRow[
                        destination + 2
                    ] =
                        sourceRow[
                            source + 2
                        ];

                    break;
                }

                case PixelFormat::BGRA8:
                {
                    const std::size_t
                        source =
                            x * 4;

                    destinationRow[
                        destination + 0
                    ] =
                        sourceRow[
                            source + 2
                        ];

                    destinationRow[
                        destination + 1
                    ] =
                        sourceRow[
                            source + 1
                        ];

                    destinationRow[
                        destination + 2
                    ] =
                        sourceRow[
                            source + 0
                        ];

                    break;
                }

                case PixelFormat::Gray8:
                {
                    const std::uint8_t
                        value =
                            sourceRow[x];

                    destinationRow[
                        destination + 0
                    ] =
                        value;

                    destinationRow[
                        destination + 1
                    ] =
                        value;

                    destinationRow[
                        destination + 2
                    ] =
                        value;

                    break;
                }

                default:

                    return false;
            }
        }
    }

    return true;
}



/*
 * Adaptive surveillance-frame enhancement.
 *
 * This is intentionally implemented with the C++ standard library only.
 * It therefore works in both the native Windows build and the Emscripten
 * build without introducing an OpenCV dependency into the detector.
 *
 * IMPORTANT:
 * The original video frame is never modified.  Enhancement is applied
 * only to the temporary RGB buffer used by YOLO.
 *
 * The normal/daytime path is left untouched.  Enhancement is enabled
 * only when the sampled frame looks like low-light, low-contrast, or
 * thermal/near-monochrome surveillance footage.
 */
bool enhanceDifficultSurveillanceFrame(
    std::vector<std::uint8_t>& rgb,
    std::size_t width,
    std::size_t height
)
{
    if (
        rgb.empty() ||
        width == 0 ||
        height == 0 ||
        rgb.size() < width * height * 3
    )
    {
        return false;
    }

    /*
     * Sample rather than scanning every pixel.  This keeps the quality
     * analysis inexpensive even for 1080p/4K input.
     */
    constexpr std::size_t SampleStep = 8;

    double meanLuma = 0.0;
    double meanChroma = 0.0;
    double meanGradient = 0.0;

    std::uint8_t minimumLuma = 255;
    std::uint8_t maximumLuma = 0;

    std::size_t samples = 0;
    std::size_t gradientSamples = 0;

    for (
        std::size_t y = 0;
        y < height;
        y += SampleStep
    )
    {
        for (
            std::size_t x = 0;
            x < width;
            x += SampleStep
        )
        {
            const std::size_t index =
                (y * width + x) * 3;

            const float r =
                static_cast<float>(
                    rgb[index + 0]
                );

            const float g =
                static_cast<float>(
                    rgb[index + 1]
                );

            const float b =
                static_cast<float>(
                    rgb[index + 2]
                );

            const float luma =
                0.299f * r +
                0.587f * g +
                0.114f * b;

            const float maxChannel =
                std::max(
                    r,
                    std::max(g, b)
                );

            const float minChannel =
                std::min(
                    r,
                    std::min(g, b)
                );

            meanLuma +=
                static_cast<double>(luma);

            meanChroma +=
                static_cast<double>(
                    maxChannel - minChannel
                );

            minimumLuma =
                std::min(
                    minimumLuma,
                    static_cast<std::uint8_t>(
                        std::clamp(
                            luma,
                            0.0f,
                            255.0f
                        )
                    )
                );

            maximumLuma =
                std::max(
                    maximumLuma,
                    static_cast<std::uint8_t>(
                        std::clamp(
                            luma,
                            0.0f,
                            255.0f
                        )
                    )
                );

            if (
                x + SampleStep < width
            )
            {
                const std::size_t rightIndex =
                    (y * width + x + SampleStep) * 3;

                const float rightLuma =
                    0.299f *
                        static_cast<float>(
                            rgb[rightIndex + 0]
                        ) +
                    0.587f *
                        static_cast<float>(
                            rgb[rightIndex + 1]
                        ) +
                    0.114f *
                        static_cast<float>(
                            rgb[rightIndex + 2]
                        );

                meanGradient +=
                    std::abs(
                        static_cast<double>(
                            luma - rightLuma
                        )
                    );

                ++gradientSamples;
            }

            if (
                y + SampleStep < height
            )
            {
                const std::size_t downIndex =
                    ((y + SampleStep) * width + x) * 3;

                const float downLuma =
                    0.299f *
                        static_cast<float>(
                            rgb[downIndex + 0]
                        ) +
                    0.587f *
                        static_cast<float>(
                            rgb[downIndex + 1]
                        ) +
                    0.114f *
                        static_cast<float>(
                            rgb[downIndex + 2]
                        );

                meanGradient +=
                    std::abs(
                        static_cast<double>(
                            luma - downLuma
                        )
                    );

                ++gradientSamples;
            }

            ++samples;
        }
    }

    if (samples == 0)
    {
        return false;
    }

    meanLuma /=
        static_cast<double>(samples);

    meanChroma /=
        static_cast<double>(samples);

    if (gradientSamples > 0)
    {
        meanGradient /=
            static_cast<double>(gradientSamples);
    }

    const float dynamicRange =
        static_cast<float>(
            maximumLuma -
            minimumLuma
        );

    /*
     * These conditions intentionally require evidence that the frame is
     * difficult.  Ordinary daytime/color footage therefore follows the
     * exact old preprocessing path.
     */
    const bool lowLight =
        meanLuma < 72.0;

    const bool lowContrast =
        dynamicRange < 48.0f;

    /*
     * Thermal/IR feeds are frequently grayscale or nearly monochrome.
     * Do not classify monochrome alone as thermal: ordinary grayscale
     * footage can still have healthy contrast.
     */
    const bool thermalLike =
        meanChroma < 12.0 &&
        dynamicRange < 160.0f;

    const bool needsVisibilityEnhancement =
        lowLight ||
        lowContrast ||
        thermalLike;

    /*
     * Blur is handled only with a very mild sharpening pass.  We do not
     * attempt to invent missing detail, and we never run this on a normal
     * sharp frame.
     */
    const bool needsMildSharpen =
        meanGradient < 7.5 &&
        dynamicRange > 18.0f;

    if (
        !needsVisibilityEnhancement &&
        !needsMildSharpen
    )
    {
        return false;
    }

    /*
     * Enhancement parameters are deliberately conservative.
     *
     * Low-light:
     *     gamma < 1 lifts dark regions.
     *
     * Thermal/low-contrast:
     *     stretch the observed luma range before gamma.
     *
     * We preserve color ratios reasonably well rather than converting
     * the frame to artificial color.
     */
    const float inputMin =
        static_cast<float>(
            minimumLuma
        );

    const float inputRange =
        std::max(
            24.0f,
            dynamicRange
        );

    const float gamma =
        lowLight || thermalLike
            ? 0.72f
            : 0.88f;

    const float contrast =
        lowContrast || thermalLike
            ? 1.18f
            : 1.05f;

    std::vector<std::uint8_t> enhanced;

    try
    {
        enhanced.resize(
            rgb.size()
        );
    }
    catch (...)
    {
        return false;
    }

    for (
        std::size_t index = 0;
        index + 2 < rgb.size();
        index += 3
    )
    {
        float r =
            static_cast<float>(
                rgb[index + 0]
            ) /
            255.0f;

        float g =
            static_cast<float>(
                rgb[index + 1]
            ) /
            255.0f;

        float b =
            static_cast<float>(
                rgb[index + 2]
            ) /
            255.0f;

        const float luma =
            0.299f * r +
            0.587f * g +
            0.114f * b;

        float normalizedLuma =
            (
                luma * 255.0f -
                inputMin
            ) /
            inputRange;

        normalizedLuma =
            std::clamp(
                normalizedLuma,
                0.0f,
                1.0f
            );

        normalizedLuma =
            std::pow(
                normalizedLuma,
                gamma
            );

        normalizedLuma =
            std::clamp(
                (
                    normalizedLuma -
                    0.5f
                ) *
                contrast +
                0.5f,
                0.0f,
                1.0f
            );

        const float originalLuma =
            std::max(
                0.001f,
                luma
            );

        const float scale =
            normalizedLuma /
            originalLuma;

        r =
            std::clamp(
                r * scale,
                0.0f,
                1.0f
            );

        g =
            std::clamp(
                g * scale,
                0.0f,
                1.0f
            );

        b =
            std::clamp(
                b * scale,
                0.0f,
                1.0f
            );

        enhanced[index + 0] =
            static_cast<std::uint8_t>(
                std::lround(
                    r * 255.0f
                )
            );

        enhanced[index + 1] =
            static_cast<std::uint8_t>(
                std::lround(
                    g * 255.0f
                )
            );

        enhanced[index + 2] =
            static_cast<std::uint8_t>(
                std::lround(
                    b * 255.0f
                )
            );
    }

    /*
     * A small edge-preserving-ish unsharp pass.  The amount is deliberately
     * low because surveillance noise must not become fake object texture.
     */
    if (needsMildSharpen)
    {
        std::vector<std::uint8_t> sharpened;

        try
        {
            sharpened =
                enhanced;
        }
        catch (...)
        {
            return false;
        }

        constexpr float SharpenAmount = 0.22f;

        for (
            std::size_t y = 1;
            y + 1 < height;
            ++y
        )
        {
            for (
                std::size_t x = 1;
                x + 1 < width;
                ++x
            )
            {
                const std::size_t index =
                    (y * width + x) * 3;

                const std::size_t left =
                    (y * width + x - 1) * 3;

                const std::size_t right =
                    (y * width + x + 1) * 3;

                const std::size_t up =
                    ((y - 1) * width + x) * 3;

                const std::size_t down =
                    ((y + 1) * width + x) * 3;

                for (
                    int channel = 0;
                    channel < 3;
                    ++channel
                )
                {
                    const float center =
                        static_cast<float>(
                            enhanced[index + channel]
                        );

                    const float neighbourAverage =
                        (
                            static_cast<float>(
                                enhanced[left + channel]
                            ) +
                            static_cast<float>(
                                enhanced[right + channel]
                            ) +
                            static_cast<float>(
                                enhanced[up + channel]
                            ) +
                            static_cast<float>(
                                enhanced[down + channel]
                            )
                        ) *
                        0.25f;

                    sharpened[index + channel] =
                        static_cast<std::uint8_t>(
                            std::clamp(
                                center +
                                (
                                    center -
                                    neighbourAverage
                                ) *
                                SharpenAmount,
                                0.0f,
                                255.0f
                            )
                        );
                }
            }
        }

        rgb =
            std::move(
                sharpened
            );
    }
    else
    {
        rgb =
            std::move(
                enhanced
            );
    }

    return true;
}


bool createLetterboxedInput(
    const AIFrame& frame,
    ncnn::Mat& input,
    float& scale,
    int& padX,
    int& padY
)
{
    std::vector<std::uint8_t>
        rgb;

    if (
        !convertFrameToRGB(
            frame,
            rgb
        )
    )
    {
        return false;
    }

    /*
     * Enhance only the temporary AI input when the frame appears to be
     * low-light, thermal/IR-like, low-contrast, or unusually soft.
     *
     * The original Frame is never changed, so displayed video, dashboard
     * crops, history images, face recognition input, and ANPR all retain
     * their existing behavior.
     */
    enhanceDifficultSurveillanceFrame(
        rgb,
        static_cast<std::size_t>(frame.width),
        static_cast<std::size_t>(frame.height)
    );

    const int sourceWidth =
        static_cast<int>(
            frame.width
        );

    const int sourceHeight =
        static_cast<int>(
            frame.height
        );

    if (
        sourceWidth <= 0 ||
        sourceHeight <= 0
    )
    {
        return false;
    }

    ncnn::Mat source =
        ncnn::Mat::from_pixels(
            rgb.data(),
            ncnn::Mat::PIXEL_RGB,
            sourceWidth,
            sourceHeight
        );

    if (
        source.empty()
    )
    {
        return false;
    }

    scale =
        std::min(
            static_cast<float>(
                INPUT_SIZE
            ) /
            static_cast<float>(
                sourceWidth
            ),
            static_cast<float>(
                INPUT_SIZE
            ) /
            static_cast<float>(
                sourceHeight
            )
        );

    const int resizedWidth =
        std::max(
            1,
            static_cast<int>(
                std::round(
                    static_cast<float>(
                        sourceWidth
                    ) *
                    scale
                )
            )
        );

    const int resizedHeight =
        std::max(
            1,
            static_cast<int>(
                std::round(
                    static_cast<float>(
                        sourceHeight
                    ) *
                    scale
                )
            )
        );

    padX =
        (
            INPUT_SIZE -
            resizedWidth
        ) /
        2;

    padY =
        (
            INPUT_SIZE -
            resizedHeight
        ) /
        2;

    ncnn::Mat resized;

    if (
        resizedWidth ==
            sourceWidth &&
        resizedHeight ==
            sourceHeight
    )
    {
        resized =
            source;
    }
    else
    {
        ncnn::resize_bilinear(
            source,
            resized,
            resizedWidth,
            resizedHeight
        );
    }

    if (
        resized.empty()
    )
    {
        return false;
    }

    const int rightPadding =
        INPUT_SIZE -
        resizedWidth -
        padX;

    const int bottomPadding =
        INPUT_SIZE -
        resizedHeight -
        padY;

    if (
        rightPadding < 0 ||
        bottomPadding < 0
    )
    {
        return false;
    }

    ncnn::Mat padded;

    ncnn::copy_make_border(
        resized,
        padded,
        padY,
        bottomPadding,
        padX,
        rightPadding,
        ncnn::BORDER_CONSTANT,
        114.0f
    );

    if (
        padded.empty()
    )
    {
        return false;
    }

    if (
        padded.w !=
            INPUT_SIZE ||
        padded.h !=
            INPUT_SIZE ||
        padded.c != 3
    )
    {
        return false;
    }

    const float normalization[] =
    {
        1.0f / 255.0f,
        1.0f / 255.0f,
        1.0f / 255.0f
    };

    padded.substract_mean_normalize(
        nullptr,
        normalization
    );

    input =
        padded;

    return true;
}


bool isVehicleClass(
    int classId
)
{
    /*
     * COCO:
     *
     * 1 = bicycle
     * 2 = car
     * 3 = motorcycle
     * 4 = airplane
     * 5 = bus
     * 6 = train
     * 7 = truck
     * 8 = boat
     *
     * For IBVAP these are all treated as VEHICLE.
     */
    switch (
        classId
    )
    {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:

            return true;

        default:

            return false;
    }
}


/*
 * Validate a decoded candidate after it has been converted
 * back into the original video coordinate system.
 *
 * IMPORTANT:
 *
 * This is deliberately conservative.
 *
 * We do NOT impose:
 *
 *     "person must be tall"
 *
 * because that would break:
 *
 *     - crouching people
 *     - sitting people
 *     - partially visible people
 *     - distant people
 *     - people near frame boundaries
 *     - moderately blurry people
 *
 * The purpose of this function is only to reject extremely
 * implausible detections that are much more likely to be
 * background structures than people.
 */
bool passesPersonValidation(
    const Candidate& candidate,
    std::uint32_t frameWidth,
    std::uint32_t frameHeight
)
{
    if (
        candidate.classId != 0
    )
    {
        return true;
    }

    if (
        frameWidth == 0 ||
        frameHeight == 0
    )
    {
        return false;
    }

    const float width =
        candidate.x2 -
        candidate.x1;

    const float height =
        candidate.y2 -
        candidate.y1;

    if (
        width <= 0.0f ||
        height <= 0.0f
    )
    {
        return false;
    }

    const float frameArea =
        static_cast<float>(
            frameWidth
        ) *
        static_cast<float>(
            frameHeight
        );

    const float boxArea =
        width *
        height;

    if (
        frameArea <= 0.0f
    )
    {
        return false;
    }

    const float normalizedArea =
        boxArea /
        frameArea;

    /*
     * Only reject truly microscopic pathological boxes.
     *
     * This threshold is intentionally extremely small so
     * distant legitimate people are not removed.
     */
    if (
        normalizedArea <
        MIN_NORMALIZED_BOX_AREA
    )
    {
        return false;
    }

    const float normalizedHeight =
        height /
        static_cast<float>(frameHeight);

    if (
        candidate.confidence < LOW_CONFIDENCE_PERSON_THRESHOLD &&
        normalizedHeight < 0.0125f
    )
    {
        return false;
    }

    const float aspectRatio =
        width /
        height;

    if (
        !std::isfinite(
            aspectRatio
        )
    )
    {
        return false;
    }

    /*
     * At lower confidence, use a stricter shape sanity check.
     *
     * Example:
     *
     *     genuine distant person:
     *         confidence = 0.40
     *         ratio      = 1.2
     *
     *     -> accepted
     *
     *     obvious wide table/bag region:
     *         confidence = 0.61
     *         ratio      = 5.0
     *
     *     -> rejected
     *
     * High-confidence detections are given more freedom because
     * unusual poses can legitimately produce wide boxes.
     */
    if (
        candidate.confidence <
        LOW_CONFIDENCE_PERSON_THRESHOLD
    )
    {
        if (
            aspectRatio >
            LOW_CONFIDENCE_PERSON_MAX_ASPECT_RATIO
        )
        {
            return false;
        }
    }
    else
    {
        /*
         * Even high-confidence person detections should not be
         * allowed to become absurdly wide/shallow.
         */
        if (
            aspectRatio >
            PERSON_MAX_ASPECT_RATIO
        )
        {
            return false;
        }
    }

    return true;
}


bool passesVehicleValidation(
    const Candidate& candidate,
    std::uint32_t frameWidth,
    std::uint32_t frameHeight
)
{
    if (
        frameWidth == 0 ||
        frameHeight == 0
    )
    {
        return false;
    }

    const float width =
        candidate.x2 - candidate.x1;

    const float height =
        candidate.y2 - candidate.y1;

    if (
        width <= 0.0f ||
        height <= 0.0f
    )
    {
        return false;
    }

    const float frameArea =
        static_cast<float>(frameWidth) *
        static_cast<float>(frameHeight);

    const float normalizedArea =
        (width * height) / frameArea;

    if (normalizedArea < 0.00003f)
    {
        return false;
    }

    const float normalizedHeight =
        height / static_cast<float>(frameHeight);

    if (
        candidate.confidence < 0.60f &&
        normalizedHeight < 0.008f
    )
    {
        return false;
    }

    const float aspectRatio =
        width / height;

    if (!std::isfinite(aspectRatio) || aspectRatio > 8.0f)
    {
        return false;
    }

    return true;
}


/*
 * Extract one YOLOv8 prediction safely.
 *
 * For a 2D ncnn Mat:
 *
 *     output.cstep
 *
 * must not be treated as the row stride.
 *
 * Use output.row() instead.
 */
bool extractPredictionValues(
    const ncnn::Mat& output,
    int predictionIndex,
    float* values
)
{
    if (
        values == nullptr
    )
    {
        return false;
    }

    if (
        output.dims != 2
    )
    {
        return false;
    }

    if (
        output.elemsize !=
        sizeof(float)
    )
    {
        return false;
    }

    if (
        output.elempack !=
        1
    )
    {
        return false;
    }

    /*
     * Standard YOLOv8 NCNN layout:
     *
     *     w = 144
     *     h = 8400
     *
     * 144 =
     *
     *     64 DFL regression values
     *     +
     *     80 class values.
     */
    if (
        output.w ==
            OUTPUT_VALUES_PER_DETECTION &&
        output.h ==
            TOTAL_PREDICTIONS
    )
    {
        if (
            predictionIndex < 0 ||
            predictionIndex >=
                output.h
        )
        {
            return false;
        }

        const float* row =
            output.row(
                predictionIndex
            );

        if (
            row == nullptr
        )
        {
            return false;
        }

        std::copy(
            row,
            row +
                OUTPUT_VALUES_PER_DETECTION,
            values
        );

        return true;
    }

    /*
     * Alternative transposed layout:
     *
     *     w = 8400
     *     h = 144
     */
    if (
        output.w ==
            TOTAL_PREDICTIONS &&
        output.h ==
            OUTPUT_VALUES_PER_DETECTION
    )
    {
        if (
            predictionIndex < 0 ||
            predictionIndex >=
                output.w
        )
        {
            return false;
        }

        for (
            int valueIndex = 0;
            valueIndex <
                OUTPUT_VALUES_PER_DETECTION;
            ++valueIndex
        )
        {
            const float* row =
                output.row(
                    valueIndex
                );

            if (
                row == nullptr
            )
            {
                return false;
            }

            values[
                valueIndex
            ] =
                row[
                    predictionIndex
                ];
        }

        return true;
    }

    return false;
}

}


struct ObjectDetector::Impl
{
    ncnn::Net network;

    bool initialized =
        false;

    std::string paramPath;

    std::string binPath;

    float confidenceThreshold =
        DEFAULT_CONFIDENCE_THRESHOLD;

    float nmsThreshold =
        DEFAULT_NMS_THRESHOLD;
};


ObjectDetector::ObjectDetector()
{
    m_impl =
        new Impl();
}


ObjectDetector::~ObjectDetector()
{
    shutdown();

    delete m_impl;

    m_impl =
        nullptr;
}


bool ObjectDetector::initialize(
    const std::string& paramPath,
    const std::string& binPath
)
{
    if (
        m_impl == nullptr
    )
    {
        return false;
    }

    shutdown();

    if (
        paramPath.empty() ||
        binPath.empty()
    )
    {
        return false;
    }

    m_impl->network.clear();

    /*
     * Keep the detector portable.
     *
     * Vulkan compute is disabled so
     * the same inference path remains
     * available for native and WASM.
     */
    m_impl->network.opt
        .use_vulkan_compute =
        false;

    const int paramResult =
        m_impl->network.load_param(
            paramPath.c_str()
        );

    if (
        paramResult != 0
    )
    {
        std::cerr
            << "IBVAP AI: failed to load NCNN param."
            << '\n'
            << "  Path: "
            << paramPath
            << '\n'
            << "  Result: "
            << paramResult
            << '\n';

        return false;
    }

    const int modelResult =
        m_impl->network.load_model(
            binPath.c_str()
        );

    if (
        modelResult != 0
    )
    {
        std::cerr
            << "IBVAP AI: failed to load NCNN model."
            << '\n'
            << "  Path: "
            << binPath
            << '\n'
            << "  Result: "
            << modelResult
            << '\n';

        m_impl->network.clear();

        return false;
    }

    m_impl->paramPath =
        paramPath;

    m_impl->binPath =
        binPath;

    m_impl->initialized =
        true;

    std::cout
        << "IBVAP AI: NCNN model loaded successfully."
        << '\n';

    return true;
}


void ObjectDetector::shutdown()
    noexcept
{
    if (
        m_impl == nullptr
    )
    {
        return;
    }

    m_impl->network.clear();

    m_impl->paramPath.clear();

    m_impl->binPath.clear();

    m_impl->initialized =
        false;
}


bool ObjectDetector::isInitialized()
    const noexcept
{
    return
        m_impl != nullptr &&
        m_impl->initialized;
}


bool ObjectDetector::detect(
    const AIFrame& frame,
    AIResult& result
)
{
    result.clear();

    if (
        !isInitialized()
    )
    {
        return false;
    }

    if (
        !frame.valid()
    )
    {
        return false;
    }

    float scale =
        1.0f;

    int padX =
        0;

    int padY =
        0;

    ncnn::Mat input;

    if (
        !createLetterboxedInput(
            frame,
            input,
            scale,
            padX,
            padY
        )
    )
    {
        std::cerr
            << "IBVAP AI: failed to create detector input."
            << '\n';

        return false;
    }

    if (
        input.empty()
    )
    {
        return false;
    }

    ncnn::Extractor extractor =
        m_impl->network
            .create_extractor();

    extractor.set_light_mode(
        true
    );

    /*
     * YOLOv8 NCNN input blob.
     */
    const int inputResult =
        extractor.input(
            "in0",
            input
        );

    if (
        inputResult != 0
    )
    {
        std::cerr
            << "IBVAP AI: failed to set NCNN input in0."
            << " result="
            << inputResult
            << '\n';

        return false;
    }

    ncnn::Mat output;

    /*
     * YOLOv8 NCNN output blob.
     */
    const int extractResult =
        extractor.extract(
            "out0",
            output
        );

    if (
        extractResult != 0
    )
    {
        std::cerr
            << "IBVAP AI: failed to extract NCNN out0."
            << " result="
            << extractResult
            << '\n';

        return false;
    }

    if (
        output.empty()
    )
    {
        std::cerr
            << "IBVAP AI: NCNN returned an empty out0 tensor."
            << '\n';

        return false;
    }

    /*
     * Diagnostic tensor information.
     */
    std::cout
        << "IBVAP AI: out0 tensor"
        << " | dims="
        << output.dims
        << " | w="
        << output.w
        << " | h="
        << output.h
        << " | c="
        << output.c
        << " | elemsize="
        << output.elemsize
        << " | elempack="
        << output.elempack
        << '\n';

    /*
     * Expected YOLOv8 NCNN output:
     *
     *     144 x 8400
     *
     * or:
     *
     *     8400 x 144
     */
    const bool validOutputShape =
        (
            output.dims ==
                2 &&
            (
                (
                    output.w ==
                        OUTPUT_VALUES_PER_DETECTION &&
                    output.h ==
                        TOTAL_PREDICTIONS
                )
                ||
                (
                    output.w ==
                        TOTAL_PREDICTIONS &&
                    output.h ==
                        OUTPUT_VALUES_PER_DETECTION
                )
            )
        );

    if (
        !validOutputShape
    )
    {
        std::cerr
            << "IBVAP AI: unsupported YOLOv8 NCNN output shape."
            << '\n'
            << "  Expected: 144 x 8400"
            << '\n'
            << "  Or:       8400 x 144"
            << '\n'
            << "  Received: "
            << output.w
            << " x "
            << output.h
            << '\n';

        return false;
    }

    std::vector<Candidate>
        candidates;

    candidates.reserve(
        256
    );

    /*
     * YOLOv8 feature levels:
     *
     * P3 = 80 x 80 = 6400
     * P4 = 40 x 40 = 1600
     * P5 = 20 x 20 =  400
     *
     * Total = 8400.
     */
    constexpr int GRID_SIZES[] =
    {
        80,
        40,
        20
    };

    constexpr int STRIDES[] =
    {
        8,
        16,
        32
    };

    int predictionIndex =
        0;

    float values[
        OUTPUT_VALUES_PER_DETECTION
    ];

    for (
        int level = 0;
        level < 3;
        ++level
    )
    {
        const int grid =
            GRID_SIZES[level];

        const int stride =
            STRIDES[level];

        for (
            int gy = 0;
            gy < grid;
            ++gy
        )
        {
            for (
                int gx = 0;
                gx < grid;
                ++gx
            )
            {
                if (
                    !extractPredictionValues(
                        output,
                        predictionIndex,
                        values
                    )
                )
                {
                    std::cerr
                        << "IBVAP AI: failed to read prediction "
                        << predictionIndex
                        << '\n';

                    return false;
                }

                ++predictionIndex;

                int bestClass =
                    -1;

                float bestScore =
                    0.0f;

                for (
                    int classId = 0;
                    classId < NUM_CLASSES;
                    ++classId
                )
                {
                    const float score =
                        sigmoid(
                            values[
                                REGRESSION_CHANNELS +
                                classId
                            ]
                        );

                    if (
                        score >
                        bestScore
                    )
                    {
                        bestScore =
                            score;

                        bestClass =
                            classId;
                    }
                }

                /*
                 * IBVAP object detector has only two valid
                 * semantic outputs:
                 *
                 *     PERSON
                 *     VEHICLE
                 *
                 * Everything else is rejected immediately.
                 */
                const bool person =
                    bestClass == 0;

                const bool vehicle =
                    isVehicleClass(
                        bestClass
                    );

                if (
                    !person &&
                    !vehicle
                )
                {
                    continue;
                }

                if (
                    bestScore <
                    m_impl->confidenceThreshold
                )
                {
                    continue;
                }

                /*
                 * YOLOv8 DFL regression:
                 *
                 * [ 0..15] left
                 * [16..31] top
                 * [32..47] right
                 * [48..63] bottom
                 */
                const float left =
                    decodeDFL(
                        values
                    ) *
                    static_cast<float>(
                        stride
                    );

                const float top =
                    decodeDFL(
                        values +
                        REGRESSION_BINS
                    ) *
                    static_cast<float>(
                        stride
                    );

                const float right =
                    decodeDFL(
                        values +
                        REGRESSION_BINS * 2
                    ) *
                    static_cast<float>(
                        stride
                    );

                const float bottom =
                    decodeDFL(
                        values +
                        REGRESSION_BINS * 3
                    ) *
                    static_cast<float>(
                        stride
                    );

                const float centerX =
                    (
                        static_cast<float>(
                            gx
                        ) +
                        0.5f
                    ) *
                    static_cast<float>(
                        stride
                    );

                const float centerY =
                    (
                        static_cast<float>(
                            gy
                        ) +
                        0.5f
                    ) *
                    static_cast<float>(
                        stride
                    );

                Candidate candidate;

                candidate.classId =
                    bestClass;

                candidate.confidence =
                    bestScore;

                candidate.x1 =
                    centerX -
                    left;

                candidate.y1 =
                    centerY -
                    top;

                candidate.x2 =
                    centerX +
                    right;

                candidate.y2 =
                    centerY +
                    bottom;

                /*
                 * Undo 640x640 letterboxing.
                 */
                candidate.x1 =
                    (
                        candidate.x1 -
                        static_cast<float>(
                            padX
                        )
                    ) /
                    scale;

                candidate.y1 =
                    (
                        candidate.y1 -
                        static_cast<float>(
                            padY
                        )
                    ) /
                    scale;

                candidate.x2 =
                    (
                        candidate.x2 -
                        static_cast<float>(
                            padX
                        )
                    ) /
                    scale;

                candidate.y2 =
                    (
                        candidate.y2 -
                        static_cast<float>(
                            padY
                        )
                    ) /
                    scale;

                /*
                 * Clamp to original frame.
                 */
                candidate.x1 =
                    std::clamp(
                        candidate.x1,
                        0.0f,
                        static_cast<float>(
                            frame.width
                        )
                    );

                candidate.y1 =
                    std::clamp(
                        candidate.y1,
                        0.0f,
                        static_cast<float>(
                            frame.height
                        )
                    );

                candidate.x2 =
                    std::clamp(
                        candidate.x2,
                        0.0f,
                        static_cast<float>(
                            frame.width
                        )
                    );

                candidate.y2 =
                    std::clamp(
                        candidate.y2,
                        0.0f,
                        static_cast<float>(
                            frame.height
                        )
                    );

                if (
                    candidate.x2 <=
                        candidate.x1 ||
                    candidate.y2 <=
                        candidate.y1
                )
                {
                    continue;
                }

                /*
                 * Apply person-only geometric sanity checking.
                 *
                 * Vehicles intentionally bypass this because
                 * wide vehicle boxes are completely normal.
                 */
                if (
                    person &&
                    !passesPersonValidation(
                        candidate,
                        frame.width,
                        frame.height
                    )
                )
                {
                    continue;
                }

                if (
                    vehicle &&
                    !passesVehicleValidation(
                        candidate,
                        frame.width,
                        frame.height
                    )
                )
                {
                    continue;
                }

                candidates.push_back(
                    candidate
                );
            }
        }
    }

    /*
     * Class-aware NMS.
     */
    nmsPerClass(
        candidates,
        m_impl->nmsThreshold
    );

    result.inputWidth =
        frame.width;

    result.inputHeight =
        frame.height;

    /*
     * AIFrame intentionally does not contain
     * timestampUs.
     *
     * UI.cpp stores the original Frame timestamp
     * separately.
     */
    result.timestampUs =
        0;

    for (
        const Candidate& candidate :
        candidates
    )
    {
        AIDetection detection;

        /*
         * The object detector exposes ONLY:
         *
         *     Person
         *     Vehicle
         *
         * No other COCO class is allowed through.
         */
        if (
            candidate.classId ==
            0
        )
        {
            detection.classId =
                AIObjectClass::Person;
        }
        else if (
            isVehicleClass(
                candidate.classId
            )
        )
        {
            detection.classId =
                AIObjectClass::Vehicle;
        }
        else
        {
            /*
             * This should never happen because candidates are
             * filtered before NMS, but keep the final safety
             * boundary anyway.
             */
            continue;
        }

        detection.confidence =
            candidate.confidence;

        detection.boundingBox.x =
            candidate.x1;

        detection.boundingBox.y =
            candidate.y1;

        detection.boundingBox.width =
            candidate.x2 -
            candidate.x1;

        detection.boundingBox.height =
            candidate.y2 -
            candidate.y1;

        detection.trackId =
            -1;

        detection.identity.clear();

        result.detections.push_back(
            std::move(
                detection
            )
        );
    }

    result.inferenceSucceeded =
        true;

    return true;
}