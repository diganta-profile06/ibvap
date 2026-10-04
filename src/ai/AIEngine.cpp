#include "AIEngine.hpp"
#include "AIFrameEnhancement.hpp"

#include "detection/objectdetector.hpp"
#include "face/FaceDetector.hpp"
#ifndef __EMSCRIPTEN__
#include "anpr/LicensePlateDetector.hpp"
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#endif

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

#ifndef __EMSCRIPTEN__

// =========================================================
// ANPR vehicle-crop configuration
// =========================================================
//
// PP-OCRv5 is a text detector.
//
// Running it against the complete 1920x1080 surveillance
// frame makes a license plate very small.
//
// Therefore YOLO vehicle detections are used as ROIs and
// PP-OCRv5 is run independently on each vehicle crop.
//
// A small margin is added around the YOLO vehicle box so
// that a plate near the edge of the vehicle detection is
// not accidentally clipped.
// =========================================================

constexpr float ANPRVehicleCropMargin = 0.10f;


// =========================================================
// Clamp normalized coordinate
// =========================================================

float clamp01(
    float value
)
{
    return std::max(
        0.0f,
        std::min(
            1.0f,
            value
        )
    );
}


// =========================================================
// Convert AIFrame to BGR cv::Mat
//
// This function is intentionally platform-independent.
//
// Supported formats:
//     BGR8
//     RGB8
//     RGBA8
//     BGRA8
//     Gray8
//
// The original frame stride is respected.
// =========================================================

bool makeBGRImage(
    const AIFrame& frame,
    cv::Mat& bgr
)
{
    if (!frame.valid())
    {
        return false;
    }

    if (frame.data == nullptr)
    {
        return false;
    }

    const int width =
        static_cast<int>(
            frame.width
        );

    const int height =
        static_cast<int>(
            frame.height
        );

    const std::size_t stride =
        static_cast<std::size_t>(
            frame.stride
        );

    if (
        width <= 0 ||
        height <= 0 ||
        stride == 0
    )
    {
        return false;
    }


    // =====================================================
    // BGR8
    // =====================================================

    if (
        frame.format ==
        PixelFormat::BGR8
    )
    {
        cv::Mat source(
            height,
            width,
            CV_8UC3,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            stride
        );

        bgr =
            source.clone();

        return !bgr.empty();
    }


    // =====================================================
    // RGB8
    // =====================================================

    if (
        frame.format ==
        PixelFormat::RGB8
    )
    {
        cv::Mat source(
            height,
            width,
            CV_8UC3,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            stride
        );

        cv::cvtColor(
            source,
            bgr,
            cv::COLOR_RGB2BGR
        );

        return !bgr.empty();
    }


    // =====================================================
    // RGBA8
    // =====================================================

    if (
        frame.format ==
        PixelFormat::RGBA8
    )
    {
        cv::Mat source(
            height,
            width,
            CV_8UC4,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            stride
        );

        cv::cvtColor(
            source,
            bgr,
            cv::COLOR_RGBA2BGR
        );

        return !bgr.empty();
    }


    // =====================================================
    // BGRA8
    // =====================================================

    if (
        frame.format ==
        PixelFormat::BGRA8
    )
    {
        cv::Mat source(
            height,
            width,
            CV_8UC4,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            stride
        );

        cv::cvtColor(
            source,
            bgr,
            cv::COLOR_BGRA2BGR
        );

        return !bgr.empty();
    }


    // =====================================================
    // Gray8
    // =====================================================

    if (
        frame.format ==
        PixelFormat::Gray8
    )
    {
        cv::Mat source(
            height,
            width,
            CV_8UC1,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            stride
        );

        cv::cvtColor(
            source,
            bgr,
            cv::COLOR_GRAY2BGR
        );

        return !bgr.empty();
    }


    return false;
}


// =========================================================
// Create vehicle crop
//
// Input bounding box is normalized:
//
//     x
//     y
//     width
//     height
//
// Output:
//     crop
//     cropX
//     cropY
//
// cropX/cropY are pixel coordinates in the original frame.
//
// The crop itself is always a contiguous BGR image.
// =========================================================

bool makeVehicleCrop(
    const cv::Mat& fullBGR,
    const AIBoundingBox& vehicleBox,
    cv::Mat& crop,
    int& cropX,
    int& cropY
)
{
    crop.release();

    cropX = 0;
    cropY = 0;


    if (
        fullBGR.empty()
    )
    {
        return false;
    }


    const int imageWidth =
        fullBGR.cols;

    const int imageHeight =
        fullBGR.rows;


    if (
        imageWidth <= 0 ||
        imageHeight <= 0
    )
    {
        return false;
    }


    // =====================================================
    // Clamp YOLO bounding box
    // =====================================================

    const float normalizedX =
        clamp01(
            vehicleBox.x
        );

    const float normalizedY =
        clamp01(
            vehicleBox.y
        );

    const float normalizedWidth =
        clamp01(
            vehicleBox.width
        );

    const float normalizedHeight =
        clamp01(
            vehicleBox.height
        );


    if (
        normalizedWidth <= 0.0f ||
        normalizedHeight <= 0.0f
    )
    {
        return false;
    }


    // =====================================================
    // Convert normalized coordinates to pixels
    // =====================================================

    float x =
        normalizedX *
        static_cast<float>(
            imageWidth
        );

    float y =
        normalizedY *
        static_cast<float>(
            imageHeight
        );

    float width =
        normalizedWidth *
        static_cast<float>(
            imageWidth
        );

    float height =
        normalizedHeight *
        static_cast<float>(
            imageHeight
        );


    // =====================================================
    // Add margin around vehicle
    //
    // This is deliberately relative to the vehicle size.
    // =====================================================

    const float marginX =
        width *
        ANPRVehicleCropMargin;

    const float marginY =
        height *
        ANPRVehicleCropMargin;


    x -=
        marginX;

    y -=
        marginY;

    width +=
        marginX *
        2.0f;

    height +=
        marginY *
        2.0f;


    // =====================================================
    // Clamp crop to image
    // =====================================================

    const float right =
        std::min(
            static_cast<float>(
                imageWidth
            ),
            x + width
        );

    const float bottom =
        std::min(
            static_cast<float>(
                imageHeight
            ),
            y + height
        );


    x =
        std::max(
            0.0f,
            x
        );

    y =
        std::max(
            0.0f,
            y
        );


    width =
        right -
        x;

    height =
        bottom -
        y;


    if (
        width < 16.0f ||
        height < 16.0f
    )
    {
        return false;
    }


    // =====================================================
    // Integer ROI
    // =====================================================

    cropX =
        static_cast<int>(
            std::floor(x)
        );

    cropY =
        static_cast<int>(
            std::floor(y)
        );


    int cropWidth =
        static_cast<int>(
            std::ceil(width)
        );

    int cropHeight =
        static_cast<int>(
            std::ceil(height)
        );


    // =====================================================
    // Final integer bounds
    // =====================================================

    cropX =
        std::max(
            0,
            std::min(
                cropX,
                imageWidth - 1
            )
        );

    cropY =
        std::max(
            0,
            std::min(
                cropY,
                imageHeight - 1
            )
        );


    cropWidth =
        std::max(
            1,
            std::min(
                cropWidth,
                imageWidth - cropX
            )
        );


    cropHeight =
        std::max(
            1,
            std::min(
                cropHeight,
                imageHeight - cropY
            )
        );


    // =====================================================
    // Crop and clone
    //
    // clone() guarantees that the resulting image is
    // contiguous and independent of the original frame.
    // =====================================================

    const cv::Rect roi(
        cropX,
        cropY,
        cropWidth,
        cropHeight
    );


    crop =
        fullBGR(
            roi
        ).clone();


    return !crop.empty();
}


// =========================================================
// Convert a cropped BGR cv::Mat to AIFrame
//
// The cv::Mat remains alive during the synchronous detector
// call, so the AIFrame's raw pointer is valid.
// =========================================================

AIFrame makeAIFrameFromBGR(
    const cv::Mat& bgr
)
{
    AIFrame frame;


    if (
        bgr.empty() ||
        bgr.type() != CV_8UC3
    )
    {
        return frame;
    }


    frame.data =
        bgr.data;

    frame.width =
        static_cast<
            std::uint32_t
        >(
            bgr.cols
        );

    frame.height =
        static_cast<
            std::uint32_t
        >(
            bgr.rows
        );

    frame.stride =
        static_cast<
            std::uint32_t
        >(
            bgr.step
        );

    frame.format =
        PixelFormat::BGR8;


    return frame;
}


// =========================================================
// Translate an ANPR detection from vehicle-crop
// coordinates back to full-frame normalized coordinates.
//
// cropPlate coordinates are normalized relative to the
// vehicle crop.
//
// cropX/cropY/cropWidth/cropHeight are pixel coordinates
// inside the original frame.
// =========================================================

AIBoundingBox translatePlateToFullFrame(
    const AIBoundingBox& cropPlate,
    int cropX,
    int cropY,
    int cropWidth,
    int cropHeight,
    int imageWidth,
    int imageHeight
)
{
    AIBoundingBox result;


    // =====================================================
    // Crop-relative pixel coordinates
    // =====================================================

    const float localX =
        clamp01(
            cropPlate.x
        ) *
        static_cast<float>(
            cropWidth
        );

    const float localY =
        clamp01(
            cropPlate.y
        ) *
        static_cast<float>(
            cropHeight
        );

    const float localWidth =
        clamp01(
            cropPlate.width
        ) *
        static_cast<float>(
            cropWidth
        );

    const float localHeight =
        clamp01(
            cropPlate.height
        ) *
        static_cast<float>(
            cropHeight
        );


    // =====================================================
    // Original-frame pixel coordinates
    // =====================================================

    const float fullX =
        static_cast<float>(
            cropX
        ) +
        localX;

    const float fullY =
        static_cast<float>(
            cropY
        ) +
        localY;


    const float fullWidth =
        localWidth;

    const float fullHeight =
        localHeight;


    // =====================================================
    // Convert back to normalized coordinates
    // =====================================================

    if (
        imageWidth > 0
    )
    {
        result.x =
            fullX /
            static_cast<float>(
                imageWidth
            );

        result.width =
            fullWidth /
            static_cast<float>(
                imageWidth
            );
    }


    if (
        imageHeight > 0
    )
    {
        result.y =
            fullY /
            static_cast<float>(
                imageHeight
            );

        result.height =
            fullHeight /
            static_cast<float>(
                imageHeight
            );
    }


    // =====================================================
    // Final clamp
    // =====================================================

    result.x =
        clamp01(
            result.x
        );

    result.y =
        clamp01(
            result.y
        );

    result.width =
        clamp01(
            result.width
        );

    result.height =
        clamp01(
            result.height
        );


    // =====================================================
    // Prevent box from extending beyond normalized frame
    // =====================================================

    result.width =
        std::min(
            result.width,
            1.0f -
            result.x
        );

    result.height =
        std::min(
            result.height,
            1.0f -
            result.y
        );


    return result;
}

#endif

} // namespace


class AIEngine::Impl
{
public:
    // =====================================================
    // AI detectors
    // =====================================================

    ObjectDetector objectDetector;

    FaceDetector faceDetector;

#ifndef __EMSCRIPTEN__
    LicensePlateDetector licensePlateDetector;
#endif

    // =====================================================
    // Initialization state
    // =====================================================

    bool objectDetectorInitialized = false;

    bool faceDetectorInitialized = false;

#ifndef __EMSCRIPTEN__
    bool licensePlateDetectorInitialized = false;
#endif

    bool initialized = false;
};


// =========================================================
// Constructor
// =========================================================

AIEngine::AIEngine()
    : m_impl(
        std::make_unique<Impl>()
    )
{
}


// =========================================================
// Destructor
// =========================================================

AIEngine::~AIEngine()
{
    shutdown();
}


// =========================================================
// Initialize
// =========================================================

bool AIEngine::initialize(
    const std::string& paramPath,
    const std::string& binPath
)
{
    if (!m_impl)
    {
        m_impl =
            std::make_unique<Impl>();
    }

    shutdown();

    // =====================================================
    // 1. YOLOv8s object detector
    // =====================================================

    std::cout
        << "AIEngine: initializing YOLO object detector..."
        << std::endl;

    if (
        !m_impl->objectDetector.initialize(
            paramPath,
            binPath
        )
    )
    {
        std::cerr
            << "AIEngine: failed to initialize "
               "YOLO object detector."
            << std::endl;

        return false;
    }

    m_impl->objectDetectorInitialized = true;

    std::cout
        << "AIEngine: YOLO object detector initialized."
        << std::endl;


    // =====================================================
    // Locate runtime models directory
    //
    // Expected runtime layout:
    //
    // models/
    // ├── detection/
    // │   ├── yolov8s.ncnn.param
    // │   └── yolov8s.ncnn.bin
    // │
    // ├── face/
    // │   ├── scrfd_500m-opt2.param
    // │   └── scrfd_500m-opt2.bin
    // │
    // └── anpr/
    //     ├── pp_OCRv5_mobile_det.ncnn.param
    //     └── pp_OCRv5_mobile_det.ncnn.bin
    //
    // paramPath is expected to point to:
    //
    // models/detection/yolov8s.ncnn.param
    //
    // Therefore:
    //
    // paramPath
    //     -> detection
    //     -> models
    //
    // =====================================================

    try
    {
        const std::filesystem::path yoloParamPath(
            paramPath
        );

        const std::filesystem::path modelsDirectory =
            yoloParamPath
                .parent_path()
                .parent_path();


        // =================================================
        // 2. SCRFD face detector
        // =================================================

        const std::filesystem::path faceDirectory =
            modelsDirectory /
            "face";

        const std::filesystem::path faceParamPath =
            faceDirectory /
            "scrfd_500m-opt2.param";

        const std::filesystem::path faceBinPath =
            faceDirectory /
            "scrfd_500m-opt2.bin";

        std::cout
            << "AIEngine: checking SCRFD face detector..."
            << std::endl;

        std::cout
            << "AIEngine: SCRFD param = "
            << faceParamPath.string()
            << std::endl;

        std::cout
            << "AIEngine: SCRFD bin   = "
            << faceBinPath.string()
            << std::endl;

        if (
            std::filesystem::exists(faceParamPath) &&
            std::filesystem::exists(faceBinPath)
        )
        {
            if (
                m_impl->faceDetector.initialize(
                    faceParamPath.string(),
                    faceBinPath.string()
                )
            )
            {
                m_impl->faceDetectorInitialized = true;

                std::cout
                    << "AIEngine: SCRFD face detector initialized."
                    << std::endl;
            }
            else
            {
                std::cerr
                    << "AIEngine: SCRFD model files were found, "
                       "but face detector initialization failed."
                    << std::endl;
            }
        }
        else
        {
            std::cerr
                << "AIEngine: SCRFD model files were not found."
                << std::endl;

            std::cerr
                << "AIEngine: expected:"
                << std::endl;

            std::cerr
                << "  "
                << faceParamPath.string()
                << std::endl;

            std::cerr
                << "  "
                << faceBinPath.string()
                << std::endl;
        }


#ifndef __EMSCRIPTEN__
        // =================================================
        // 3. PP-OCRv5 ANPR detector
        // =================================================

        const std::filesystem::path anprDirectory =
            modelsDirectory /
            "anpr";

        const std::filesystem::path anprParamPath =
            anprDirectory /
            "pp_OCRv5_mobile_det.ncnn.param";

        const std::filesystem::path anprBinPath =
            anprDirectory /
            "pp_OCRv5_mobile_det.ncnn.bin";

        std::cout
            << "AIEngine: checking PP-OCRv5 ANPR detector..."
            << std::endl;

        std::cout
            << "AIEngine: ANPR param = "
            << anprParamPath.string()
            << std::endl;

        std::cout
            << "AIEngine: ANPR bin   = "
            << anprBinPath.string()
            << std::endl;

        if (
            std::filesystem::exists(anprParamPath) &&
            std::filesystem::exists(anprBinPath)
        )
        {
            if (
                m_impl->licensePlateDetector.initialize(
                    anprParamPath.string(),
                    anprBinPath.string()
                )
            )
            {
                m_impl->licensePlateDetectorInitialized =
                    true;

                std::cout
                    << "AIEngine: PP-OCRv5 ANPR detector initialized."
                    << std::endl;
            }
            else
            {
                std::cerr
                    << "AIEngine: ANPR model files were found, "
                       "but ANPR detector initialization failed."
                    << std::endl;
            }
        }
        else
        {
            std::cerr
                << "AIEngine: ANPR model files were not found."
                << std::endl;

            std::cerr
                << "AIEngine: expected:"
                << std::endl;

            std::cerr
                << "  "
                << anprParamPath.string()
                << std::endl;

            std::cerr
                << "  "
                << anprBinPath.string()
                << std::endl;
        }
#endif
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "AIEngine: exception while locating "
               "secondary AI models: "
            << exception.what()
            << std::endl;
    }


    // =====================================================
    // Engine initialization state
    //
    // YOLO remains the required primary detector.
    //
    // SCRFD and ANPR are optional secondary detectors.
    //
    // Therefore a missing/failing SCRFD or ANPR model
    // does NOT destroy the working YOLO system.
    // =====================================================

    m_impl->initialized =
        m_impl->objectDetectorInitialized;

    std::cout
        << "AIEngine: initialization complete."
        << std::endl;

    std::cout
        << "AIEngine: YOLO = "
        << (
            m_impl->objectDetectorInitialized
                ? "READY"
                : "OFF"
        )
        << std::endl;

    std::cout
        << "AIEngine: SCRFD = "
        << (
            m_impl->faceDetectorInitialized
                ? "READY"
                : "OFF"
        )
        << std::endl;

#ifndef __EMSCRIPTEN__
    std::cout
        << "AIEngine: ANPR = "
        << (
            m_impl->licensePlateDetectorInitialized
                ? "READY"
                : "OFF"
        )
        << std::endl;
#endif

    return m_impl->initialized;
}


// =========================================================
// Shutdown
// =========================================================

void AIEngine::shutdown() noexcept
{
    if (!m_impl)
    {
        return;
    }

#ifndef __EMSCRIPTEN__
    // =====================================================
    // ANPR detector
    // =====================================================

    if (
        m_impl->licensePlateDetectorInitialized
    )
    {
        m_impl->licensePlateDetector.shutdown();

        m_impl->licensePlateDetectorInitialized =
            false;
    }


#endif

    // =====================================================
    // Face detector
    // =====================================================

    if (
        m_impl->faceDetectorInitialized
    )
    {
        m_impl->faceDetector.shutdown();

        m_impl->faceDetectorInitialized =
            false;
    }


    // =====================================================
    // Object detector
    // =====================================================

    if (
        m_impl->objectDetectorInitialized
    )
    {
        m_impl->objectDetector.shutdown();

        m_impl->objectDetectorInitialized =
            false;
    }

    m_impl->initialized = false;
}


// =========================================================
// Is initialized
// =========================================================

bool AIEngine::isInitialized() const noexcept
{
    return
        m_impl != nullptr &&
        m_impl->initialized;
}


// =========================================================
// Inference
// =========================================================

bool AIEngine::infer(
    const AIFrame& frame,
    AIResult& result
)
{
    result.clear();

    if (!m_impl)
    {
        return false;
    }

    if (!m_impl->initialized)
    {
        return false;
    }

    if (!frame.valid())
    {
        return false;
    }

    // Keep one enhanced inference-only view alive for the secondary
    // detectors. YOLO has an equivalent cross-platform preprocessing pass
    // in its input builder. The source pixels used by rendering/history are
    // left untouched, and ordinary frames are passed through unchanged.
    std::vector<std::uint8_t> enhancedFrameStorage;
    const AIFrame inferenceFrame =
        makeEnhancedAIFrame(frame, enhancedFrameStorage);

    bool inferenceSucceeded = false;


    // =====================================================
    // 1. YOLOv8s object detection
    // =====================================================

    if (
        m_impl->objectDetectorInitialized
    )
    {
        if (
            m_impl->objectDetector.detect(
                frame,
                result
            )
        )
        {
            // =================================================
            // IBVAP object policy:
            //
            // YOLO is used here only for PERSON and VEHICLE.
            // Any other class must never reach the UI.
            //
            // A moderate confidence floor also removes weak
            // background false positives (trees, bags, poles,
            // shadows, etc.) while retaining reasonably
            // distant/blurry people and vehicles.
            // =================================================

            constexpr float PersonConfidenceThreshold = 0.55f;
            constexpr float VehicleConfidenceThreshold = 0.50f;

            std::vector<AIDetection> filteredDetections;
            filteredDetections.reserve(result.detections.size());

            for (
                const AIDetection& detection :
                result.detections
            )
            {
                if (
                    detection.classId ==
                    AIObjectClass::Person
                )
                {
                    if (
                        detection.confidence >=
                        PersonConfidenceThreshold
                    )
                    {
                        filteredDetections.push_back(
                            detection
                        );
                    }

                    continue;
                }

                if (
                    detection.classId ==
                    AIObjectClass::Vehicle
                )
                {
                    if (
                        detection.confidence >=
                        VehicleConfidenceThreshold
                    )
                    {
                        filteredDetections.push_back(
                            detection
                        );
                    }

                    continue;
                }

                // Explicitly discard every other YOLO class.
            }

            result.detections =
                std::move(
                    filteredDetections
                );

            inferenceSucceeded = true;
        }
    }


    // =====================================================
    // 2. SCRFD face detection
    // =====================================================
    //
    // IMPORTANT:
    //
    // Use the vector overload.
    //
    // Do NOT use:
    //
    // faceDetector.detect(frame, result)
    //
    // because that overload can clear AIResult.
    //
    // We want to preserve YOLO detections and append faces.
    //
    // =====================================================

    if (
        m_impl->faceDetectorInitialized
    )
    {
        std::vector<FaceDetection> faces;

        if (
            m_impl->faceDetector.detect(
                inferenceFrame,
                faces
            )
        )
        {
            inferenceSucceeded = true;

            for (
                const FaceDetection& face :
                faces
            )
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
                    std::move(detection)
                );
            }
        }
    }


#ifndef __EMSCRIPTEN__
    // =====================================================
    // 3. License plate candidate detection
    // =====================================================
    //
    // IMPORTANT CHANGE:
    //
    // PP-OCRv5 is NOT run only against the entire
    // 1920x1080 surveillance frame anymore.
    //
    // First we collect the YOLO vehicle detections.
    //
    // Each vehicle is cropped from the original frame.
    //
    // PP-OCRv5 then analyzes that vehicle crop.
    //
    // Finally its result is translated back to the
    // original frame coordinate system.
    //
    // This gives the OCR detector substantially more
    // pixels belonging to the vehicle/license plate.
    //
    // =====================================================

    if (
        m_impl->licensePlateDetectorInitialized
    )
    {
        // =================================================
        // Collect vehicle detections BEFORE adding ANPR
        // detections to result.detections.
        //
        // This prevents vector reallocation from invalidating
        // references while we append license plates.
        // =================================================

        std::vector<AIBoundingBox> vehicleBoxes;

        for (
            const AIDetection& detection :
            result.detections
        )
        {
            if (
                detection.classId ==
                AIObjectClass::Vehicle
            )
            {
                vehicleBoxes.push_back(
                    detection.boundingBox
                );
            }
        }


        // =================================================
        // Convert original frame to BGR once.
        //
        // The same BGR image is then used for every vehicle
        // crop.
        // =================================================

        cv::Mat fullBGR;

        if (
            !makeBGRImage(
                inferenceFrame,
                fullBGR
            )
        )
        {
            std::cerr
                << "AIEngine: unable to create BGR frame "
                   "for vehicle-crop ANPR."
                << std::endl;
        }
        else
        {
            const int imageWidth =
                fullBGR.cols;

            const int imageHeight =
                fullBGR.rows;


            // =================================================
            // Process every detected vehicle
            // =================================================

            for (
                const AIBoundingBox& vehicleBox :
                vehicleBoxes
            )
            {
                cv::Mat vehicleCrop;

                int cropX = 0;

                int cropY = 0;


                if (
                    !makeVehicleCrop(
                        fullBGR,
                        vehicleBox,
                        vehicleCrop,
                        cropX,
                        cropY
                    )
                )
                {
                    continue;
                }


                if (
                    vehicleCrop.empty()
                )
                {
                    continue;
                }


                // =================================================
                // Construct AIFrame for the vehicle crop.
                //
                // vehicleCrop remains alive until after detect()
                // returns, so the AIFrame pointer is valid.
                // =================================================

                AIFrame vehicleAIFrame =
                    makeAIFrameFromBGR(
                        vehicleCrop
                    );


                if (
                    !vehicleAIFrame.valid()
                )
                {
                    continue;
                }


                // =================================================
                // Run PP-OCRv5 on vehicle crop
                // =================================================

                std::vector<
                    LicensePlateDetection
                > plates;


                if (
                    !m_impl->licensePlateDetector.detect(
                        vehicleAIFrame,
                        plates
                    )
                )
                {
                    continue;
                }


                if (
                    plates.empty()
                )
                {
                    continue;
                }


                inferenceSucceeded =
                    true;


                // =================================================
                // Vehicle crop dimensions
                // =================================================

                const int cropWidth =
                    vehicleCrop.cols;

                const int cropHeight =
                    vehicleCrop.rows;


                // =================================================
                // Translate every detected text/plate region
                // back into original-frame coordinates.
                // =================================================

                for (
                    const LicensePlateDetection& plate :
                    plates
                )
                {
                    AIDetection detection;


                    detection.classId =
                        AIObjectClass::LicensePlate;


                    detection.confidence =
                        plate.confidence;


                    detection.boundingBox =
                        translatePlateToFullFrame(
                            plate.boundingBox,
                            cropX,
                            cropY,
                            cropWidth,
                            cropHeight,
                            imageWidth,
                            imageHeight
                        );


                    detection.trackId =
                        -1;


                    detection.identity.clear();


                    // =============================================
                    // Reject invalid translated boxes
                    // =============================================

                    if (
                        detection.boundingBox.width <= 0.0f ||
                        detection.boundingBox.height <= 0.0f
                    )
                    {
                        continue;
                    }


                    result.detections.push_back(
                        std::move(
                            detection
                        )
                    );
                }
            }
        }
    }


    // =====================================================
    // Result metadata
    // =====================================================

#endif

    result.inferenceSucceeded =
        inferenceSucceeded;

    result.inputWidth =
        frame.width;

    result.inputHeight =
        frame.height;

    return inferenceSucceeded;
}
