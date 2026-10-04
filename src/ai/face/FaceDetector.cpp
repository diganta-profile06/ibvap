#include "FaceDetector.hpp"

#include <ncnn/net.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr int kInputWidth = 640;
constexpr int kInputHeight = 640;

constexpr float kScoreThreshold = 0.30f;
constexpr float kNmsThreshold = 0.45f;

constexpr int kAnchorCount = 2;

struct Anchor
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct Proposal
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float confidence = 0.0f;
};

struct DetectionLevel
{
    const char* scoreBlob;
    const char* bboxBlob;

    int baseSize;
    int stride;
};

float clampFloat(
    float value,
    float minimum,
    float maximum
)
{
    return std::max(
        minimum,
        std::min(value, maximum)
    );
}

float intersectionArea(
    const Proposal& a,
    const Proposal& b
)
{
    const float x0 = std::max(a.x0, b.x0);
    const float y0 = std::max(a.y0, b.y0);

    const float x1 = std::min(a.x1, b.x1);
    const float y1 = std::min(a.y1, b.y1);

    const float width =
        std::max(
            0.0f,
            x1 - x0
        );

    const float height =
        std::max(
            0.0f,
            y1 - y0
        );

    return width * height;
}

float area(
    const Proposal& proposal
)
{
    const float width =
        std::max(
            0.0f,
            proposal.x1 - proposal.x0
        );

    const float height =
        std::max(
            0.0f,
            proposal.y1 - proposal.y0
        );

    return width * height;
}

void sortProposals(
    std::vector<Proposal>& proposals
)
{
    std::sort(
        proposals.begin(),
        proposals.end(),
        [](
            const Proposal& a,
            const Proposal& b
        )
        {
            return a.confidence >
                   b.confidence;
        }
    );
}

void applyNms(
    const std::vector<Proposal>& proposals,
    std::vector<int>& picked
)
{
    picked.clear();

    if (proposals.empty())
    {
        return;
    }

    std::vector<float> areas(
        proposals.size()
    );

    for (std::size_t i = 0;
         i < proposals.size();
         ++i)
    {
        areas[i] = area(proposals[i]);
    }

    for (std::size_t i = 0;
         i < proposals.size();
         ++i)
    {
        bool keep = true;

        for (const int pickedIndex : picked)
        {
            if (pickedIndex < 0 ||
                static_cast<std::size_t>(
                    pickedIndex
                ) >= proposals.size())
            {
                continue;
            }

            const std::size_t selectedIndex =
                static_cast<std::size_t>(
                    pickedIndex
                );

            const float intersection =
                intersectionArea(
                    proposals[i],
                    proposals[selectedIndex]
                );

            const float unionArea =
                areas[i] +
                areas[selectedIndex] -
                intersection;

            if (unionArea <= 0.0f)
            {
                continue;
            }

            const float overlap =
                intersection / unionArea;

            if (overlap > kNmsThreshold)
            {
                keep = false;
                break;
            }
        }

        if (keep)
        {
            picked.push_back(
                static_cast<int>(i)
            );
        }
    }
}

std::vector<Anchor> generateAnchors(
    int baseSize
)
{
    std::vector<Anchor> anchors;

    anchors.reserve(
        kAnchorCount
    );

    constexpr float ratio = 1.0f;

    const int ratioWidth =
        static_cast<int>(
            std::round(
                static_cast<float>(baseSize) /
                std::sqrt(ratio)
            )
        );

    const int ratioHeight =
        static_cast<int>(
            std::round(
                static_cast<float>(ratioWidth) *
                ratio
            )
        );

    constexpr float scales[kAnchorCount] =
    {
        1.0f,
        2.0f
    };

    for (const float scale : scales)
    {
        const float width =
            static_cast<float>(ratioWidth) *
            scale;

        const float height =
            static_cast<float>(ratioHeight) *
            scale;

        Anchor anchor;

        anchor.x0 =
            -width * 0.5f;

        anchor.y0 =
            -height * 0.5f;

        anchor.x1 =
            width * 0.5f;

        anchor.y1 =
            height * 0.5f;

        anchors.push_back(
            anchor
        );
    }

    return anchors;
}

bool makeContiguousRGB(
    const AIFrame& frame,
    std::vector<std::uint8_t>& rgb
)
{
    if (!frame.valid())
    {
        return false;
    }

    const std::size_t bytesPerPixel =
        [&]()
        {
            switch (frame.format)
            {
            case PixelFormat::RGB8:
            case PixelFormat::BGR8:
                return std::size_t(3);

            case PixelFormat::RGBA8:
            case PixelFormat::BGRA8:
                return std::size_t(4);

            case PixelFormat::Gray8:
                return std::size_t(1);

            default:
                return std::size_t(0);
            }
        }();

    if (bytesPerPixel == 0)
    {
        return false;
    }

    const std::size_t minimumStride =
        static_cast<std::size_t>(
            frame.width
        ) *
        bytesPerPixel;

    if (frame.stride < minimumStride)
    {
        return false;
    }

    /*
     * AIFrame contains only a pointer to the
     * original Frame's data. It does not expose
     * byteSize(), so the size check must not be
     * performed here.
     *
     * Frame::valid() has already been checked by
     * AIFrame::fromFrame().
     */

    const std::size_t outputStride =
        static_cast<std::size_t>(
            frame.width
        ) * 3;

    const std::size_t outputSize =
        outputStride *
        static_cast<std::size_t>(
            frame.height
        );

    try
    {
        rgb.resize(
            outputSize
        );
    }
    catch (...)
    {
        return false;
    }

    /*
     * AIFrame::data is the actual data pointer.
     * It is not a data() member function.
     */
    const std::uint8_t* source =
        frame.data;

    if (source == nullptr)
    {
        rgb.clear();
        return false;
    }

    for (std::uint32_t y = 0;
         y < frame.height;
         ++y)
    {
        const std::uint8_t* sourceRow =
            source +
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(
                frame.stride
            );

        std::uint8_t* destinationRow =
            rgb.data() +
            static_cast<std::size_t>(y) *
            outputStride;

        for (std::uint32_t x = 0;
             x < frame.width;
             ++x)
        {
            const std::size_t sourceIndex =
                static_cast<std::size_t>(x) *
                bytesPerPixel;

            const std::size_t destinationIndex =
                static_cast<std::size_t>(x) *
                3;

            switch (frame.format)
            {
            case PixelFormat::RGB8:

                destinationRow[
                    destinationIndex + 0
                ] =
                    sourceRow[
                        sourceIndex + 0
                    ];

                destinationRow[
                    destinationIndex + 1
                ] =
                    sourceRow[
                        sourceIndex + 1
                    ];

                destinationRow[
                    destinationIndex + 2
                ] =
                    sourceRow[
                        sourceIndex + 2
                    ];

                break;

            case PixelFormat::BGR8:

                destinationRow[
                    destinationIndex + 0
                ] =
                    sourceRow[
                        sourceIndex + 2
                    ];

                destinationRow[
                    destinationIndex + 1
                ] =
                    sourceRow[
                        sourceIndex + 1
                    ];

                destinationRow[
                    destinationIndex + 2
                ] =
                    sourceRow[
                        sourceIndex + 0
                    ];

                break;

            case PixelFormat::RGBA8:

                destinationRow[
                    destinationIndex + 0
                ] =
                    sourceRow[
                        sourceIndex + 0
                    ];

                destinationRow[
                    destinationIndex + 1
                ] =
                    sourceRow[
                        sourceIndex + 1
                    ];

                destinationRow[
                    destinationIndex + 2
                ] =
                    sourceRow[
                        sourceIndex + 2
                    ];

                break;

            case PixelFormat::BGRA8:

                destinationRow[
                    destinationIndex + 0
                ] =
                    sourceRow[
                        sourceIndex + 2
                    ];

                destinationRow[
                    destinationIndex + 1
                ] =
                    sourceRow[
                        sourceIndex + 1
                    ];

                destinationRow[
                    destinationIndex + 2
                ] =
                    sourceRow[
                        sourceIndex + 0
                    ];

                break;

            case PixelFormat::Gray8:

                destinationRow[
                    destinationIndex + 0
                ] =
                    sourceRow[
                        sourceIndex
                    ];

                destinationRow[
                    destinationIndex + 1
                ] =
                    sourceRow[
                        sourceIndex
                    ];

                destinationRow[
                    destinationIndex + 2
                ] =
                    sourceRow[
                        sourceIndex
                    ];

                break;

            default:
                return false;
            }
        }
    }

    return true;
}

bool generateLevelProposals(
    const ncnn::Mat& scoreBlob,
    const ncnn::Mat& bboxBlob,
    int baseSize,
    int featureStride,
    float scoreThreshold,
    std::vector<Proposal>& proposals
)
{
    if (scoreBlob.empty() ||
        bboxBlob.empty())
    {
        return false;
    }

    if (scoreBlob.dims != 3 ||
        bboxBlob.dims != 3)
    {
        return false;
    }

    if (scoreBlob.c != kAnchorCount)
    {
        return false;
    }

    if (bboxBlob.c !=
        kAnchorCount * 4)
    {
        return false;
    }

    if (scoreBlob.w <= 0 ||
        scoreBlob.h <= 0)
    {
        return false;
    }

    if (bboxBlob.w != scoreBlob.w ||
        bboxBlob.h != scoreBlob.h)
    {
        return false;
    }

    if (scoreBlob.elemsize != sizeof(float) ||
        bboxBlob.elemsize != sizeof(float))
    {
        return false;
    }

    if (scoreBlob.elempack != 1 ||
        bboxBlob.elempack != 1)
    {
        return false;
    }

    const std::vector<Anchor> anchors =
        generateAnchors(
            baseSize
        );

    const int featureWidth =
        scoreBlob.w;

    const int featureHeight =
        scoreBlob.h;

    for (int anchorIndex = 0;
         anchorIndex < kAnchorCount;
         ++anchorIndex)
    {
        const Anchor& anchor =
            anchors[
                static_cast<std::size_t>(
                    anchorIndex
                )
            ];

        const ncnn::Mat score =
            scoreBlob.channel(
                anchorIndex
            );

        const ncnn::Mat bbox =
            bboxBlob.channel_range(
                anchorIndex * 4,
                4
            );

        if (score.empty() ||
            bbox.empty())
        {
            return false;
        }

        for (int y = 0;
             y < featureHeight;
             ++y)
        {
            const float anchorY =
                anchor.y0 +
                static_cast<float>(y) *
                static_cast<float>(
                    featureStride
                );

            for (int x = 0;
                 x < featureWidth;
                 ++x)
            {
                const int index =
                    y * featureWidth + x;

                const float confidence =
                    score[index];

                if (!std::isfinite(
                        confidence
                    ) ||
                    confidence <
                        scoreThreshold)
                {
                    continue;
                }

                const float anchorX =
                    anchor.x0 +
                    static_cast<float>(x) *
                    static_cast<float>(
                        featureStride
                    );

                const float anchorWidth =
                    anchor.x1 -
                    anchor.x0;

                const float anchorHeight =
                    anchor.y1 -
                    anchor.y0;

                const float centerX =
                    anchorX +
                    anchorWidth * 0.5f;

                const float centerY =
                    anchorY +
                    anchorHeight * 0.5f;

                const float dx =
                    bbox.channel(0)[index] *
                    static_cast<float>(
                        featureStride
                    );

                const float dy =
                    bbox.channel(1)[index] *
                    static_cast<float>(
                        featureStride
                    );

                const float dw =
                    bbox.channel(2)[index] *
                    static_cast<float>(
                        featureStride
                    );

                const float dh =
                    bbox.channel(3)[index] *
                    static_cast<float>(
                        featureStride
                    );

                if (!std::isfinite(dx) ||
                    !std::isfinite(dy) ||
                    !std::isfinite(dw) ||
                    !std::isfinite(dh))
                {
                    continue;
                }

                Proposal proposal;

                proposal.x0 =
                    centerX - dx;

                proposal.y0 =
                    centerY - dy;

                proposal.x1 =
                    centerX + dw;

                proposal.y1 =
                    centerY + dh;

                proposal.confidence =
                    confidence;

                if (proposal.x1 <=
                        proposal.x0 ||
                    proposal.y1 <=
                        proposal.y0)
                {
                    continue;
                }

                proposals.push_back(
                    proposal
                );
            }
        }
    }

    return true;
}

} // namespace

struct FaceDetector::Impl
{
    ncnn::Net net;

    bool initialized = false;
};

FaceDetector::FaceDetector()
    : m_impl(
        std::make_unique<Impl>()
    )
{
}

FaceDetector::~FaceDetector()
{
    shutdown();
}

bool FaceDetector::initialize(
    const std::string& paramPath,
    const std::string& binPath
)
{
    shutdown();

    if (!m_impl)
    {
        m_impl =
            std::make_unique<Impl>();
    }

    m_impl->net.clear();

    /*
     * Keep the detector portable across:
     *
     * - Windows
     * - Linux
     * - macOS
     * - WebAssembly
     *
     * Vulkan acceleration can be enabled later
     * as an optimization after the complete
     * multi-detector pipeline is stable.
     */
    m_impl->net.opt.use_vulkan_compute =
        false;

    m_impl->net.opt.use_fp16_storage =
        true;

    m_impl->net.opt.use_fp16_packed =
        true;

    m_impl->net.opt.use_fp16_arithmetic =
        true;

    const int paramResult =
        m_impl->net.load_param(
            paramPath.c_str()
        );

    if (paramResult != 0)
    {
        std::cerr
            << "FaceDetector: failed to load "
               "SCRFD param: "
            << paramPath
            << '\n';

        return false;
    }

    const int modelResult =
        m_impl->net.load_model(
            binPath.c_str()
        );

    if (modelResult != 0)
    {
        std::cerr
            << "FaceDetector: failed to load "
               "SCRFD model: "
            << binPath
            << '\n';

        m_impl->net.clear();

        return false;
    }

    m_impl->initialized = true;

    std::cout
        << "FaceDetector: SCRFD initialized"
        << '\n';

    return true;
}

void FaceDetector::shutdown() noexcept
{
    if (!m_impl)
    {
        return;
    }

    m_impl->net.clear();

    m_impl->initialized =
        false;
}

bool FaceDetector::isInitialized() const noexcept
{
    return
        m_impl != nullptr &&
        m_impl->initialized;
}

bool FaceDetector::detect(
    const AIFrame& frame,
    std::vector<FaceDetection>& faces
)
{
    faces.clear();

    if (!isInitialized())
    {
        return false;
    }

    if (!frame.valid())
    {
        return false;
    }

    if (frame.width == 0 ||
        frame.height == 0)
    {
        return false;
    }

    std::vector<std::uint8_t> rgb;

    if (!makeContiguousRGB(
            frame,
            rgb
        ))
    {
        return false;
    }

    if (rgb.empty())
    {
        return false;
    }

    const int originalWidth =
        static_cast<int>(
            frame.width
        );

    const int originalHeight =
        static_cast<int>(
            frame.height
        );

    int resizedWidth =
        originalWidth;

    int resizedHeight =
        originalHeight;

    float scale =
        1.0f;

    /*
     * Preserve aspect ratio and resize the
     * longest dimension to 640.
     */
    if (resizedWidth >
        resizedHeight)
    {
        scale =
            static_cast<float>(
                kInputWidth
            ) /
            static_cast<float>(
                resizedWidth
            );

        resizedWidth =
            kInputWidth;

        resizedHeight =
            static_cast<int>(
                static_cast<float>(
                    resizedHeight
                ) *
                scale
            );
    }
    else
    {
        scale =
            static_cast<float>(
                kInputHeight
            ) /
            static_cast<float>(
                resizedHeight
            );

        resizedHeight =
            kInputHeight;

        resizedWidth =
            static_cast<int>(
                static_cast<float>(
                    resizedWidth
                ) *
                scale
            );
    }

    resizedWidth =
        std::max(
            1,
            resizedWidth
        );

    resizedHeight =
        std::max(
            1,
            resizedHeight
        );

    ncnn::Mat input =
        ncnn::Mat::from_pixels_resize(
            rgb.data(),
            ncnn::Mat::PIXEL_RGB,
            originalWidth,
            originalHeight,
            resizedWidth,
            resizedHeight
        );

    if (input.empty())
    {
        return false;
    }

    /*
     * SCRFD expects the image to be padded to
     * a multiple of 32.
     *
     * Padding is symmetric, matching the
     * official SCRFD ncnn preprocessing.
     */
    const int widthPadding =
        (
            (
                resizedWidth + 31
            ) / 32
        ) *
        32 -
        resizedWidth;

    const int heightPadding =
        (
            (
                resizedHeight + 31
            ) / 32
        ) *
        32 -
        resizedHeight;

    const int leftPadding =
        widthPadding / 2;

    const int rightPadding =
        widthPadding -
        leftPadding;

    const int topPadding =
        heightPadding / 2;

    const int bottomPadding =
        heightPadding -
        topPadding;

    ncnn::Mat paddedInput;

    ncnn::copy_make_border(
        input,
        paddedInput,
        topPadding,
        bottomPadding,
        leftPadding,
        rightPadding,
        ncnn::BORDER_CONSTANT,
        0.0f
    );

    if (paddedInput.empty())
    {
        return false;
    }

    const float meanValues[3] =
    {
        127.5f,
        127.5f,
        127.5f
    };

    const float normValues[3] =
    {
        1.0f / 128.0f,
        1.0f / 128.0f,
        1.0f / 128.0f
    };

    paddedInput.substract_mean_normalize(
        meanValues,
        normValues
    );

    ncnn::Extractor extractor =
        m_impl->net.create_extractor();

    extractor.set_light_mode(
        true
    );

    if (extractor.input(
            "input.1",
            paddedInput
        ) != 0)
    {
        return false;
    }

    /*
     * SCRFD 500M output levels.
     *
     * stride 8:
     *   score = 412
     *   bbox  = 415
     *
     * stride 16:
     *   score = 474
     *   bbox  = 477
     *
     * stride 32:
     *   score = 536
     *   bbox  = 539
     */
    const DetectionLevel levels[3] =
    {
        {
            "412",
            "415",
            16,
            8
        },

        {
            "474",
            "477",
            64,
            16
        },

        {
            "536",
            "539",
            256,
            32
        }
    };

    std::vector<Proposal> proposals;

    proposals.reserve(
        5000
    );

    for (const DetectionLevel& level :
         levels)
    {
        ncnn::Mat scoreBlob;
        ncnn::Mat bboxBlob;

        if (extractor.extract(
                level.scoreBlob,
                scoreBlob
            ) != 0)
        {
            return false;
        }

        if (extractor.extract(
                level.bboxBlob,
                bboxBlob
            ) != 0)
        {
            return false;
        }

        if (!generateLevelProposals(
                scoreBlob,
                bboxBlob,
                level.baseSize,
                level.stride,
                kScoreThreshold,
                proposals
            ))
        {
            return false;
        }
    }

    if (proposals.empty())
    {
        return true;
    }

    sortProposals(
        proposals
    );

    std::vector<int> picked;

    applyNms(
        proposals,
        picked
    );

    faces.reserve(
        picked.size()
    );

    for (const int proposalIndex :
         picked)
    {
        if (proposalIndex < 0 ||
            static_cast<std::size_t>(
                proposalIndex
            ) >= proposals.size())
        {
            continue;
        }

        const Proposal& proposal =
            proposals[
                static_cast<std::size_t>(
                    proposalIndex
                )
            ];

        /*
         * Remove the symmetric SCRFD padding,
         * then map from resized coordinates back
         * to the original frame.
         */
        float x0 =
            (
                proposal.x0 -
                static_cast<float>(
                    leftPadding
                )
            ) /
            scale;

        float y0 =
            (
                proposal.y0 -
                static_cast<float>(
                    topPadding
                )
            ) /
            scale;

        float x1 =
            (
                proposal.x1 -
                static_cast<float>(
                    leftPadding
                )
            ) /
            scale;

        float y1 =
            (
                proposal.y1 -
                static_cast<float>(
                    topPadding
                )
            ) /
            scale;

        x0 = clampFloat(
            x0,
            0.0f,
            static_cast<float>(
                originalWidth - 1
            )
        );

        y0 = clampFloat(
            y0,
            0.0f,
            static_cast<float>(
                originalHeight - 1
            )
        );

        x1 = clampFloat(
            x1,
            0.0f,
            static_cast<float>(
                originalWidth - 1
            )
        );

        y1 = clampFloat(
            y1,
            0.0f,
            static_cast<float>(
                originalHeight - 1
            )
        );

        if (x1 <= x0 ||
            y1 <= y0)
        {
            continue;
        }

        FaceDetection face;

        face.boundingBox.x =
            x0;

        face.boundingBox.y =
            y0;

        face.boundingBox.width =
            x1 - x0;

        face.boundingBox.height =
            y1 - y0;

        face.confidence =
            proposal.confidence;

        /*
         * The supplied SCRFD model graph has
         * face score and bbox heads only.
         * It does not contain landmark heads.
         */
        face.hasLandmarks =
            false;

        faces.push_back(
            face
        );
    }

    return true;
}

bool FaceDetector::detect(
    const AIFrame& frame,
    AIResult& result
)
{
    result.clear();

    if (!isInitialized())
    {
        return false;
    }

    std::vector<FaceDetection> faces;

    if (!detect(
            frame,
            faces
        ))
    {
        return false;
    }

    result.inputWidth =
        frame.width;

    result.inputHeight =
        frame.height;

    result.inferenceSucceeded =
        true;

    result.detections.reserve(
        faces.size()
    );

    for (const FaceDetection& face :
         faces)
    {
        AIDetection detection;

        detection.classId =
            AIObjectClass::Face;

        detection.confidence =
            face.confidence;

        detection.boundingBox =
            face.boundingBox;

        detection.trackId =
            -1;

        detection.identity.clear();

        result.detections.push_back(
            std::move(
                detection
            )
        );
    }

    return true;
}