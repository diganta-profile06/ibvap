#include "LicensePlateDetector.hpp"

#include <ncnn/net.h>
#include <ncnn/mat.h>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

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

// =========================================================
// PP-OCRv5 configuration
// =========================================================

constexpr int TargetSize = 960;
constexpr int TargetStride = 32;

constexpr float ProbabilityThreshold = 0.30f;
constexpr float BoxThreshold = 0.60f;

constexpr float EnlargeRatio = 1.95f;

constexpr int MaximumCandidates = 1000;
constexpr std::size_t MaximumPlates = 32;


// =========================================================
// OpenCV fallback configuration
//
// This fallback is intentionally independent from any
// operating-system API. It works directly on the vehicle
// crop supplied by AIEngine.
//
// The primary target is the yellow Indian-style plate visible
// in the current demonstration video.
// =========================================================

constexpr float YellowMinimumAreaRatio = 0.0010f;
constexpr float YellowMaximumAreaRatio = 0.20f;

constexpr float YellowMinimumAspectRatio = 2.0f;
constexpr float YellowMaximumAspectRatio = 7.5f;

constexpr float YellowMinimumFillRatio = 0.35f;

constexpr float WhiteMinimumAreaRatio = 0.0008f;
constexpr float WhiteMaximumAreaRatio = 0.12f;

constexpr float WhiteMinimumAspectRatio = 2.0f;
constexpr float WhiteMaximumAspectRatio = 7.0f;

constexpr int MinimumPlatePixelWidth = 24;
constexpr int MinimumPlatePixelHeight = 8;


// =========================================================
// Internal candidate
// =========================================================

struct Candidate
{
    cv::RotatedRect rotatedBox;

    float confidence = 0.0f;

    int orientation = 0;
};


// =========================================================
// Fallback candidate
// =========================================================

struct FallbackCandidate
{
    cv::Rect rectangle;

    float confidence = 0.0f;
};


// =========================================================
// Clamp
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
// Finite check
// =========================================================

bool isFinite(
    float value
)
{
    return std::isfinite(value);
}


// =========================================================
// Convert AIFrame -> BGR ncnn::Mat
// =========================================================

bool makeBGR(
    const AIFrame& frame,
    ncnn::Mat& output
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

    if (
        width <= 0 ||
        height <= 0
    )
    {
        return false;
    }


    // -----------------------------------------------------
    // BGR8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::BGR8
    )
    {
        output =
            ncnn::Mat::from_pixels(
                frame.data,
                ncnn::Mat::PIXEL_BGR,
                width,
                height
            );

        return !output.empty();
    }


    // -----------------------------------------------------
    // RGB8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::RGB8
    )
    {
        output =
            ncnn::Mat::from_pixels(
                frame.data,
                ncnn::Mat::PIXEL_RGB2BGR,
                width,
                height
            );

        return !output.empty();
    }


    // -----------------------------------------------------
    // RGBA8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::RGBA8
    )
    {
        output =
            ncnn::Mat::from_pixels(
                frame.data,
                ncnn::Mat::PIXEL_RGBA2BGR,
                width,
                height
            );

        return !output.empty();
    }


    // -----------------------------------------------------
    // BGRA8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::BGRA8
    )
    {
        output =
            ncnn::Mat::from_pixels(
                frame.data,
                ncnn::Mat::PIXEL_BGRA2BGR,
                width,
                height
            );

        return !output.empty();
    }


    // -----------------------------------------------------
    // Gray8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::Gray8
    )
    {
        output =
            ncnn::Mat::from_pixels(
                frame.data,
                ncnn::Mat::PIXEL_GRAY2BGR,
                width,
                height
            );

        return !output.empty();
    }


    return false;
}


// =========================================================
// Convert AIFrame -> OpenCV BGR image
//
// Unlike makeBGR(), this function explicitly respects the
// source stride. The resulting image is cloned so it owns
// its memory and can safely be processed by OpenCV.
//
// This is standard cross-platform OpenCV code.
// =========================================================

bool makeOpenCVBGR(
    const AIFrame& frame,
    cv::Mat& output
)
{
    output.release();


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


    if (
        width <= 0 ||
        height <= 0
    )
    {
        return false;
    }


    const std::size_t minimumStride =
        static_cast<std::size_t>(
            width
        ) *
        3U;


    // -----------------------------------------------------
    // BGR8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::BGR8
    )
    {
        if (
            frame.stride <
            minimumStride
        )
        {
            return false;
        }


        cv::Mat source(
            height,
            width,
            CV_8UC3,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            static_cast<std::size_t>(
                frame.stride
            )
        );


        output =
            source.clone();


        return !output.empty();
    }


    // -----------------------------------------------------
    // RGB8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::RGB8
    )
    {
        if (
            frame.stride <
            minimumStride
        )
        {
            return false;
        }


        cv::Mat source(
            height,
            width,
            CV_8UC3,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            static_cast<std::size_t>(
                frame.stride
            )
        );


        cv::cvtColor(
            source,
            output,
            cv::COLOR_RGB2BGR
        );


        return !output.empty();
    }


    // -----------------------------------------------------
    // RGBA8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::RGBA8
    )
    {
        const std::size_t requiredStride =
            static_cast<std::size_t>(
                width
            ) *
            4U;


        if (
            frame.stride <
            requiredStride
        )
        {
            return false;
        }


        cv::Mat source(
            height,
            width,
            CV_8UC4,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            static_cast<std::size_t>(
                frame.stride
            )
        );


        cv::cvtColor(
            source,
            output,
            cv::COLOR_RGBA2BGR
        );


        return !output.empty();
    }


    // -----------------------------------------------------
    // BGRA8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::BGRA8
    )
    {
        const std::size_t requiredStride =
            static_cast<std::size_t>(
                width
            ) *
            4U;


        if (
            frame.stride <
            requiredStride
        )
        {
            return false;
        }


        cv::Mat source(
            height,
            width,
            CV_8UC4,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            static_cast<std::size_t>(
                frame.stride
            )
        );


        cv::cvtColor(
            source,
            output,
            cv::COLOR_BGRA2BGR
        );


        return !output.empty();
    }


    // -----------------------------------------------------
    // Gray8
    // -----------------------------------------------------

    if (
        frame.format ==
        PixelFormat::Gray8
    )
    {
        if (
            frame.stride <
            static_cast<std::size_t>(
                width
            )
        )
        {
            return false;
        }


        cv::Mat source(
            height,
            width,
            CV_8UC1,
            const_cast<std::uint8_t*>(
                frame.data
            ),
            static_cast<std::size_t>(
                frame.stride
            )
        );


        cv::cvtColor(
            source,
            output,
            cv::COLOR_GRAY2BGR
        );


        return !output.empty();
    }


    return false;
}


// =========================================================
// Calculate contour score
//
// This is the same basic operation used by Tencent's
// PP-OCRv5 ncnn example.
//
// The score measures the average probability inside
// the contour mask.
// =========================================================

double contourScore(
    const cv::Mat& probabilityImage,
    const std::vector<cv::Point>& contour
)
{
    if (
        probabilityImage.empty() ||
        contour.empty()
    )
    {
        return 0.0;
    }


    cv::Rect rect =
        cv::boundingRect(
            contour
        );


    if (
        rect.x < 0
    )
    {
        rect.x = 0;
    }

    if (
        rect.y < 0
    )
    {
        rect.y = 0;
    }


    if (
        rect.x +
        rect.width >
        probabilityImage.cols
    )
    {
        rect.width =
            probabilityImage.cols -
            rect.x;
    }


    if (
        rect.y +
        rect.height >
        probabilityImage.rows
    )
    {
        rect.height =
            probabilityImage.rows -
            rect.y;
    }


    if (
        rect.width <= 0 ||
        rect.height <= 0
    )
    {
        return 0.0;
    }


    const cv::Mat binROI =
        probabilityImage(
            rect
        );


    cv::Mat mask =
        cv::Mat::zeros(
            rect.height,
            rect.width,
            CV_8U
        );


    std::vector<cv::Point>
        roiContour;


    roiContour.reserve(
        contour.size()
    );


    for (
        const cv::Point& point :
        contour
    )
    {
        roiContour.emplace_back(
            point.x -
            rect.x,

            point.y -
            rect.y
        );
    }


    std::vector<
        std::vector<cv::Point>
    > roiContours =
    {
        roiContour
    };


    cv::fillPoly(
        mask,
        roiContours,
        cv::Scalar(255)
    );


    const double score =
        cv::mean(
            binROI,
            mask
        ).val[0];


    return score / 255.0;
}


// =========================================================
// Convert rotated rectangle to axis-aligned bounding box
//
// AIBoundingBox currently has no rotation field, so the
// smallest enclosing axis-aligned rectangle is returned.
// =========================================================

cv::Rect2f rotatedRectToBoundingRect(
    const cv::RotatedRect& rotated
)
{
    cv::Point2f points[4];

    rotated.points(
        points
    );


    float minX =
        points[0].x;

    float maxX =
        points[0].x;

    float minY =
        points[0].y;

    float maxY =
        points[0].y;


    for (
        int i = 1;
        i < 4;
        ++i
    )
    {
        minX =
            std::min(
                minX,
                points[i].x
            );

        maxX =
            std::max(
                maxX,
                points[i].x
            );

        minY =
            std::min(
                minY,
                points[i].y
            );

        maxY =
            std::max(
                maxY,
                points[i].y
            );
    }


    return cv::Rect2f(
        minX,
        minY,
        std::max(
            0.0f,
            maxX - minX
        ),
        std::max(
            0.0f,
            maxY - minY
        )
    );
}


// =========================================================
// Calculate edge density inside a candidate rectangle
// =========================================================

float calculateEdgeDensity(
    const cv::Mat& gray,
    const cv::Rect& rectangle
)
{
    if (
        gray.empty() ||
        rectangle.width <= 0 ||
        rectangle.height <= 0
    )
    {
        return 0.0f;
    }


    cv::Rect safeRectangle =
        rectangle;


    safeRectangle &= cv::Rect(
        0,
        0,
        gray.cols,
        gray.rows
    );


    if (
        safeRectangle.width <= 0 ||
        safeRectangle.height <= 0
    )
    {
        return 0.0f;
    }


    cv::Mat roi =
        gray(
            safeRectangle
        );


    cv::Mat edges;


    cv::Canny(
        roi,
        edges,
        60.0,
        180.0
    );


    if (
        edges.empty()
    )
    {
        return 0.0f;
    }


    const double edgePixels =
        static_cast<double>(
            cv::countNonZero(
                edges
            )
        );


    const double totalPixels =
        static_cast<double>(
            edges.rows
        ) *
        static_cast<double>(
            edges.cols
        );


    if (
        totalPixels <= 0.0
    )
    {
        return 0.0f;
    }


    return clamp01(
        static_cast<float>(
            edgePixels /
            totalPixels
        )
    );
}


// =========================================================
// Add fallback detection to output
// =========================================================

void appendFallbackDetection(
    const cv::Rect& rectangle,
    float confidence,
    int imageWidth,
    int imageHeight,
    std::vector<LicensePlateDetection>& plates
)
{
    if (
        imageWidth <= 0 ||
        imageHeight <= 0
    )
    {
        return;
    }


    cv::Rect safeRectangle =
        rectangle;


    safeRectangle &=
        cv::Rect(
            0,
            0,
            imageWidth,
            imageHeight
        );


    if (
        safeRectangle.width <= 1 ||
        safeRectangle.height <= 1
    )
    {
        return;
    }


    LicensePlateDetection detection;


    detection.confidence =
        clamp01(
            confidence
        );


    detection.boundingBox.x =
        clamp01(
            static_cast<float>(
                safeRectangle.x
            ) /
            static_cast<float>(
                imageWidth
            )
        );


    detection.boundingBox.y =
        clamp01(
            static_cast<float>(
                safeRectangle.y
            ) /
            static_cast<float>(
                imageHeight
            )
        );


    detection.boundingBox.width =
        clamp01(
            static_cast<float>(
                safeRectangle.width
            ) /
            static_cast<float>(
                imageWidth
            )
        );


    detection.boundingBox.height =
        clamp01(
            static_cast<float>(
                safeRectangle.height
            ) /
            static_cast<float>(
                imageHeight
            )
        );


    if (
        detection.boundingBox.width <= 0.0f ||
        detection.boundingBox.height <= 0.0f
    )
    {
        return;
    }


    plates.push_back(
        detection
    );
}


// =========================================================
// Yellow license plate fallback
//
// Designed for the vehicle crop.
//
// The detector combines:
//
// 1. Yellow color mask
// 2. Rectangular geometry
// 3. Aspect ratio
// 4. Mask fill ratio
// 5. Edge density
//
// This is localization only. OCR comes later.
// =========================================================

bool detectYellowPlateFallback(
    const cv::Mat& bgr,
    std::vector<LicensePlateDetection>& plates
)
{
    if (
        bgr.empty() ||
        bgr.channels() != 3
    )
    {
        return false;
    }


    const int imageWidth =
        bgr.cols;

    const int imageHeight =
        bgr.rows;


    if (
        imageWidth <= 0 ||
        imageHeight <= 0
    )
    {
        return false;
    }


    cv::Mat hsv;


    cv::cvtColor(
        bgr,
        hsv,
        cv::COLOR_BGR2HSV
    );


    if (
        hsv.empty()
    )
    {
        return false;
    }


    // -----------------------------------------------------
    // Yellow / orange-yellow plate range.
    //
    // OpenCV hue is 0..179.
    // -----------------------------------------------------

    cv::Mat yellowMask;


    cv::inRange(
        hsv,
        cv::Scalar(
            12,
            55,
            70
        ),
        cv::Scalar(
            42,
            255,
            255
        ),
        yellowMask
    );


    if (
        yellowMask.empty()
    )
    {
        return false;
    }


    // -----------------------------------------------------
    // Small morphology only.
    //
    // Do not use a huge kernel because the surrounding
    // yellow vehicle body must not be merged into the plate.
    // -----------------------------------------------------

    const cv::Mat closeKernel =
        cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(
                5,
                3
            )
        );


    const cv::Mat openKernel =
        cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(
                3,
                3
            )
        );


    cv::morphologyEx(
        yellowMask,
        yellowMask,
        cv::MORPH_CLOSE,
        closeKernel
    );


    cv::morphologyEx(
        yellowMask,
        yellowMask,
        cv::MORPH_OPEN,
        openKernel
    );


    // -----------------------------------------------------
    // Contours
    // -----------------------------------------------------

    std::vector<
        std::vector<cv::Point>
    > contours;


    cv::findContours(
        yellowMask,
        contours,
        cv::RETR_EXTERNAL,
        cv::CHAIN_APPROX_SIMPLE
    );


    if (
        contours.empty()
    )
    {
        return false;
    }


    std::vector<FallbackCandidate>
        candidates;


    candidates.reserve(
        contours.size()
    );


    const double imageArea =
        static_cast<double>(
            imageWidth
        ) *
        static_cast<double>(
            imageHeight
        );


    if (
        imageArea <= 0.0
    )
    {
        return false;
    }


    cv::Mat gray;


    cv::cvtColor(
        bgr,
        gray,
        cv::COLOR_BGR2GRAY
    );


    for (
        const std::vector<cv::Point>& contour :
        contours
    )
    {
        if (
            contour.size() < 4
        )
        {
            continue;
        }


        const double contourArea =
            cv::contourArea(
                contour
            );


        if (
            contourArea <= 0.0
        )
        {
            continue;
        }


        const double areaRatio =
            contourArea /
            imageArea;


        if (
            areaRatio <
            YellowMinimumAreaRatio ||
            areaRatio >
            YellowMaximumAreaRatio
        )
        {
            continue;
        }


        cv::Rect rectangle =
            cv::boundingRect(
                contour
            );


        if (
            rectangle.width <
            MinimumPlatePixelWidth ||
            rectangle.height <
            MinimumPlatePixelHeight
        )
        {
            continue;
        }


        const float width =
            static_cast<float>(
                rectangle.width
            );


        const float height =
            static_cast<float>(
                rectangle.height
            );


        if (
            width <= 0.0f ||
            height <= 0.0f
        )
        {
            continue;
        }


        const float aspectRatio =
            std::max(
                width / height,
                height / width
            );


        if (
            aspectRatio <
            YellowMinimumAspectRatio ||
            aspectRatio >
            YellowMaximumAspectRatio
        )
        {
            continue;
        }


        const double rectangleArea =
            static_cast<double>(
                rectangle.width
            ) *
            static_cast<double>(
                rectangle.height
            );


        if (
            rectangleArea <= 0.0
        )
        {
            continue;
        }


        // -------------------------------------------------
        // Yellow fill inside rectangle
        // -------------------------------------------------

        cv::Mat maskROI =
            yellowMask(
                rectangle
            );


        const double yellowPixels =
            static_cast<double>(
                cv::countNonZero(
                    maskROI
                )
            );


        const float yellowFill =
            clamp01(
                static_cast<float>(
                    yellowPixels /
                    rectangleArea
                )
            );


        if (
            yellowFill <
            YellowMinimumFillRatio
        )
        {
            continue;
        }


        // -------------------------------------------------
        // Edge density
        //
        // License plates contain strong character/edge
        // structure compared with a smooth painted body.
        // -------------------------------------------------

        const float edgeDensity =
            calculateEdgeDensity(
                gray,
                rectangle
            );


        // -------------------------------------------------
        // Aspect score.
        //
        // Typical plate shape is wider than tall.
        // -------------------------------------------------

        const float idealAspect =
            3.0f;


        const float aspectDifference =
            std::fabs(
                std::log(
                    std::max(
                        0.001f,
                        aspectRatio /
                        idealAspect
                    )
                )
            );


        const float aspectScore =
            clamp01(
                1.0f -
                aspectDifference /
                1.0f
            );


        // -------------------------------------------------
        // Fill score.
        // -------------------------------------------------

        const float fillScore =
            clamp01(
                yellowFill
            );


        // -------------------------------------------------
        // Edge score.
        //
        // Do not make this too strong because video
        // compression can reduce character edges.
        // -------------------------------------------------

        const float edgeScore =
            clamp01(
                edgeDensity *
                5.0f
            );


        // -------------------------------------------------
        // Combined confidence.
        // -------------------------------------------------

        const float confidence =
            clamp01(
                fillScore * 0.50f +
                aspectScore * 0.30f +
                edgeScore * 0.20f
            );


        if (
            confidence <
            0.48f
        )
        {
            continue;
        }


        FallbackCandidate candidate;


        candidate.rectangle =
            rectangle;


        candidate.confidence =
            confidence;


        candidates.push_back(
            candidate
        );
    }


    if (
        candidates.empty()
    )
    {
        return false;
    }


    // -----------------------------------------------------
    // Highest confidence first.
    // -----------------------------------------------------

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const FallbackCandidate& a,
            const FallbackCandidate& b
        )
        {
            return
                a.confidence >
                b.confidence;
        }
    );


    // -----------------------------------------------------
    // Keep only the strongest candidate.
    //
    // A vehicle crop should normally contain one plate.
    // This also prevents multiple yellow vehicle-body
    // regions from creating many detections.
    // -----------------------------------------------------

    const FallbackCandidate& best =
        candidates.front();


    appendFallbackDetection(
        best.rectangle,
        best.confidence,
        imageWidth,
        imageHeight,
        plates
    );


    return !plates.empty();
}


// =========================================================
// White/light license plate fallback
//
// This is deliberately stricter than the yellow detector.
// It exists for white plates but is not allowed to dominate
// the yellow detector.
// =========================================================

bool detectWhitePlateFallback(
    const cv::Mat& bgr,
    std::vector<LicensePlateDetection>& plates
)
{
    if (
        bgr.empty() ||
        bgr.channels() != 3
    )
    {
        return false;
    }


    const int imageWidth =
        bgr.cols;

    const int imageHeight =
        bgr.rows;


    if (
        imageWidth <= 0 ||
        imageHeight <= 0
    )
    {
        return false;
    }


    cv::Mat hsv;


    cv::cvtColor(
        bgr,
        hsv,
        cv::COLOR_BGR2HSV
    );


    // -----------------------------------------------------
    // White/light regions:
    //
    // relatively low saturation
    // relatively high brightness
    // -----------------------------------------------------

    cv::Mat whiteMask;


    cv::inRange(
        hsv,
        cv::Scalar(
            0,
            0,
            150
        ),
        cv::Scalar(
            179,
            85,
            255
        ),
        whiteMask
    );


    if (
        whiteMask.empty()
    )
    {
        return false;
    }


    const cv::Mat closeKernel =
        cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(
                5,
                3
            )
        );


    cv::morphologyEx(
        whiteMask,
        whiteMask,
        cv::MORPH_CLOSE,
        closeKernel
    );


    std::vector<
        std::vector<cv::Point>
    > contours;


    cv::findContours(
        whiteMask,
        contours,
        cv::RETR_EXTERNAL,
        cv::CHAIN_APPROX_SIMPLE
    );


    if (
        contours.empty()
    )
    {
        return false;
    }


    const double imageArea =
        static_cast<double>(
            imageWidth
        ) *
        static_cast<double>(
            imageHeight
        );


    cv::Mat gray;


    cv::cvtColor(
        bgr,
        gray,
        cv::COLOR_BGR2GRAY
    );


    std::vector<FallbackCandidate>
        candidates;


    for (
        const std::vector<cv::Point>& contour :
        contours
    )
    {
        if (
            contour.size() < 4
        )
        {
            continue;
        }


        const double contourArea =
            cv::contourArea(
                contour
            );


        const double areaRatio =
            contourArea /
            imageArea;


        if (
            areaRatio <
            WhiteMinimumAreaRatio ||
            areaRatio >
            WhiteMaximumAreaRatio
        )
        {
            continue;
        }


        const cv::Rect rectangle =
            cv::boundingRect(
                contour
            );


        if (
            rectangle.width <
            MinimumPlatePixelWidth ||
            rectangle.height <
            MinimumPlatePixelHeight
        )
        {
            continue;
        }


        const float width =
            static_cast<float>(
                rectangle.width
            );


        const float height =
            static_cast<float>(
                rectangle.height
            );


        if (
            width <= 0.0f ||
            height <= 0.0f
        )
        {
            continue;
        }


        const float aspectRatio =
            std::max(
                width / height,
                height / width
            );


        if (
            aspectRatio <
            WhiteMinimumAspectRatio ||
            aspectRatio >
            WhiteMaximumAspectRatio
        )
        {
            continue;
        }


        const double rectangleArea =
            static_cast<double>(
                rectangle.width
            ) *
            static_cast<double>(
                rectangle.height
            );


        if (
            rectangleArea <= 0.0
        )
        {
            continue;
        }


        cv::Mat maskROI =
            whiteMask(
                rectangle
            );


        const float whiteFill =
            clamp01(
                static_cast<float>(
                    cv::countNonZero(
                        maskROI
                    ) /
                    rectangleArea
                )
            );


        if (
            whiteFill <
            0.48f
        )
        {
            continue;
        }


        const float edgeDensity =
            calculateEdgeDensity(
                gray,
                rectangle
            );


        const float aspectDifference =
            std::fabs(
                std::log(
                    std::max(
                        0.001f,
                        aspectRatio /
                        3.0f
                    )
                )
            );


        const float aspectScore =
            clamp01(
                1.0f -
                aspectDifference
            );


        const float confidence =
            clamp01(
                whiteFill * 0.45f +
                aspectScore * 0.30f +
                clamp01(
                    edgeDensity * 5.0f
                ) *
                0.25f
            );


        // White fallback must be reasonably strong.
        if (
            confidence <
            0.60f
        )
        {
            continue;
        }


        FallbackCandidate candidate;


        candidate.rectangle =
            rectangle;


        candidate.confidence =
            confidence;


        candidates.push_back(
            candidate
        );
    }


    if (
        candidates.empty()
    )
    {
        return false;
    }


    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const FallbackCandidate& a,
            const FallbackCandidate& b
        )
        {
            return
                a.confidence >
                b.confidence;
        }
    );


    const FallbackCandidate& best =
        candidates.front();


    // Slightly lower than yellow because the white
    // fallback has a higher chance of confusing bright
    // vehicle/body regions with a plate.
    const float confidence =
        std::min(
            0.89f,
            best.confidence
        );


    appendFallbackDetection(
        best.rectangle,
        confidence,
        imageWidth,
        imageHeight,
        plates
    );


    return !plates.empty();
}


// =========================================================
// OpenCV fallback
//
// Priority:
//   1. Yellow plate
//   2. White/light plate
//
// PP-OCRv5 remains the primary detector and calls this only
// when it did not produce a candidate.
// =========================================================

bool detectWithOpenCVFallback(
    const AIFrame& frame,
    std::vector<LicensePlateDetection>& plates
)
{
    cv::Mat bgr;


    if (
        !makeOpenCVBGR(
            frame,
            bgr
        )
    )
    {
        return false;
    }


    // -----------------------------------------------------
    // Yellow plate first.
    // -----------------------------------------------------

    if (
        detectYellowPlateFallback(
            bgr,
            plates
        )
    )
    {
        return true;
    }


    // -----------------------------------------------------
    // White/light plate second.
    // -----------------------------------------------------

    if (
        detectWhitePlateFallback(
            bgr,
            plates
        )
    )
    {
        return true;
    }


    return false;
}

} // namespace


// =========================================================
// Implementation
// =========================================================

struct LicensePlateDetector::Impl
{
    ncnn::Net network;

    bool initialized = false;
};


// =========================================================
// Constructor
// =========================================================

LicensePlateDetector::LicensePlateDetector()
    : m_impl(
        std::make_unique<Impl>()
    )
{
}


// =========================================================
// Destructor
// =========================================================

LicensePlateDetector::~LicensePlateDetector()
{
    shutdown();
}


// =========================================================
// Initialize
// =========================================================

bool LicensePlateDetector::initialize(
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


    m_impl->network.clear();


    // -----------------------------------------------------
    // Cross-platform CPU inference
    //
    // No Windows-specific APIs.
    // -----------------------------------------------------

    m_impl->network.opt.use_vulkan_compute =
        false;

    m_impl->network.opt.use_fp16_packed =
        true;

    m_impl->network.opt.use_fp16_storage =
        true;

    m_impl->network.opt.use_fp16_arithmetic =
        true;


    // -----------------------------------------------------
    // Load parameter
    // -----------------------------------------------------

    const int paramResult =
        m_impl->network.load_param(
            paramPath.c_str()
        );


    if (
        paramResult != 0
    )
    {
        std::cerr
            << "LicensePlateDetector: failed to load "
               "parameter file: "
            << paramPath
            << std::endl;

        return false;
    }


    // -----------------------------------------------------
    // Load model
    // -----------------------------------------------------

    const int modelResult =
        m_impl->network.load_model(
            binPath.c_str()
        );


    if (
        modelResult != 0
    )
    {
        std::cerr
            << "LicensePlateDetector: failed to load "
               "model file: "
            << binPath
            << std::endl;

        m_impl->network.clear();

        return false;
    }


    m_impl->initialized =
        true;


    std::cout
        << "LicensePlateDetector: "
           "PP-OCRv5 detector initialized."
        << std::endl;


    std::cout
        << "LicensePlateDetector: "
           "OpenCV plate-localization fallback enabled."
        << std::endl;


    return true;
}


// =========================================================
// Shutdown
// =========================================================

void LicensePlateDetector::shutdown() noexcept
{
    if (!m_impl)
    {
        return;
    }


    m_impl->network.clear();

    m_impl->initialized =
        false;
}


// =========================================================
// Is initialized
// =========================================================

bool LicensePlateDetector::isInitialized() const noexcept
{
    return
        m_impl != nullptr &&
        m_impl->initialized;
}


// =========================================================
// Detect
// =========================================================

bool LicensePlateDetector::detect(
    const AIFrame& frame,
    std::vector<LicensePlateDetection>& plates
)
{
    plates.clear();


    if (!isInitialized())
    {
        return false;
    }


    if (!frame.valid())
    {
        return false;
    }


    // =====================================================
    // Convert frame to BGR
    // =====================================================

    ncnn::Mat bgr;


    if (
        !makeBGR(
            frame,
            bgr
        )
    )
    {
        return false;
    }


    const int imageWidth =
        static_cast<int>(
            frame.width
        );

    const int imageHeight =
        static_cast<int>(
            frame.height
        );


    if (
        imageWidth <= 0 ||
        imageHeight <= 0
    )
    {
        return false;
    }


    // =====================================================
    // PP-OCRv5 preprocessing
    // =====================================================

    int resizedWidth =
        imageWidth;

    int resizedHeight =
        imageHeight;

    float scale =
        1.0f;


    if (
        std::max(
            resizedWidth,
            resizedHeight
        ) >
        TargetSize
    )
    {
        if (
            resizedWidth >
            resizedHeight
        )
        {
            scale =
                static_cast<float>(
                    TargetSize
                ) /
                static_cast<float>(
                    resizedWidth
                );

            resizedWidth =
                TargetSize;

            resizedHeight =
                static_cast<int>(
                    static_cast<float>(
                        imageHeight
                    ) *
                    scale
                );
        }
        else
        {
            scale =
                static_cast<float>(
                    TargetSize
                ) /
                static_cast<float>(
                    resizedHeight
                );

            resizedHeight =
                TargetSize;

            resizedWidth =
                static_cast<int>(
                    static_cast<float>(
                        imageWidth
                    ) *
                    scale
                );
        }
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


    // =====================================================
    // Resize
    //
    // Your installed ncnn does not provide the overload
    // used by Tencent's example, so use resize_bilinear().
    // =====================================================

    ncnn::Mat input;


    if (
        resizedWidth != imageWidth ||
        resizedHeight != imageHeight
    )
    {
        ncnn::resize_bilinear(
            bgr,
            input,
            resizedWidth,
            resizedHeight
        );
    }
    else
    {
        input =
            bgr;
    }


    if (input.empty())
    {
        return false;
    }


    // =====================================================
    // Pad to multiple of 32
    // =====================================================

    const int widthPadding =
        (
            (
                resizedWidth +
                TargetStride -
                1
            ) /
            TargetStride
        ) *
        TargetStride -
        resizedWidth;


    const int heightPadding =
        (
            (
                resizedHeight +
                TargetStride -
                1
            ) /
            TargetStride
        ) *
        TargetStride -
        resizedHeight;


    ncnn::Mat padded;


    ncnn::copy_make_border(
        input,
        padded,
        heightPadding / 2,
        heightPadding -
            heightPadding / 2,
        widthPadding / 2,
        widthPadding -
            widthPadding / 2,
        ncnn::BORDER_CONSTANT,
        114.0f
    );


    if (padded.empty())
    {
        return false;
    }


    // =====================================================
    // Official PP-OCRv5 normalization
    // =====================================================

    const float meanValues[3] =
    {
        0.485f * 255.0f,
        0.456f * 255.0f,
        0.406f * 255.0f
    };


    const float normalizeValues[3] =
    {
        1.0f /
        0.229f /
        255.0f,

        1.0f /
        0.224f /
        255.0f,

        1.0f /
        0.225f /
        255.0f
    };


    padded.substract_mean_normalize(
        meanValues,
        normalizeValues
    );


    // =====================================================
    // Inference
    // =====================================================

    ncnn::Extractor extractor =
        m_impl->network.create_extractor();


    extractor.set_light_mode(
        true
    );


    if (
        extractor.input(
            "in0",
            padded
        ) != 0
    )
    {
        std::cerr
            << "LicensePlateDetector: failed to set "
               "input 'in0'."
            << std::endl;

        return false;
    }


    ncnn::Mat output;


    if (
        extractor.extract(
            "out0",
            output
        ) != 0
    )
    {
        std::cerr
            << "LicensePlateDetector: failed to extract "
               "output 'out0'."
            << std::endl;

        return false;
    }


    if (output.empty())
    {
        return false;
    }


    if (
        output.elemsize !=
        sizeof(float)
    )
    {
        std::cerr
            << "LicensePlateDetector: unexpected output "
               "element size: "
            << output.elemsize
            << std::endl;

        return false;
    }


    if (
        output.elempack !=
        1
    )
    {
        std::cerr
            << "LicensePlateDetector: unexpected output "
               "packing: "
            << output.elempack
            << std::endl;

        return false;
    }


    if (
        output.w <= 0 ||
        output.h <= 0
    )
    {
        return false;
    }


    // =====================================================
    // Diagnostic BEFORE denormalization
    //
    // This is the actual probability range.
    // =====================================================

    static bool printedOutputInfo =
        false;


    if (!printedOutputInfo)
    {
        printedOutputInfo =
            true;


        float minimumProbability =
            1.0f;

        float maximumProbability =
            0.0f;


        const float* raw =
            output.channel(0);


        if (raw != nullptr)
        {
            const std::size_t count =
                static_cast<std::size_t>(
                    output.w
                ) *
                static_cast<std::size_t>(
                    output.h
                );


            for (
                std::size_t i = 0;
                i < count;
                ++i
            )
            {
                const float value =
                    raw[i];


                if (!isFinite(value))
                {
                    continue;
                }


                minimumProbability =
                    std::min(
                        minimumProbability,
                        value
                    );


                maximumProbability =
                    std::max(
                        maximumProbability,
                        value
                    );
            }
        }


        std::cout
            << "LicensePlateDetector: output = "
            << output.w
            << " x "
            << output.h
            << " x "
            << output.c
            << std::endl;


        std::cout
            << "LicensePlateDetector: "
               "probability range before denormalization = "
            << minimumProbability
            << " .. "
            << maximumProbability
            << std::endl;
    }


    // =====================================================
    // PP-OCRv5 output denormalization
    // =====================================================

    const float denormalizeValues[1] =
    {
        255.0f
    };


    output.substract_mean_normalize(
        nullptr,
        denormalizeValues
    );


    // =====================================================
    // Convert probability map to 8-bit image
    // =====================================================

    cv::Mat probabilityImage(
        output.h,
        output.w,
        CV_8UC1
    );


    if (
        probabilityImage.empty()
    )
    {
        return false;
    }


    output.to_pixels(
        probabilityImage.data,
        ncnn::Mat::PIXEL_GRAY
    );


    // =====================================================
    // Threshold probability map
    // =====================================================

    cv::Mat bitmap;


    cv::threshold(
        probabilityImage,
        bitmap,
        ProbabilityThreshold * 255.0,
        255,
        cv::THRESH_BINARY
    );


    if (
        bitmap.empty()
    )
    {
        return false;
    }


    // =====================================================
    // Find contours
    //
    // This is the official PP-OCRv5 ncnn approach.
    // =====================================================

    std::vector<
        std::vector<cv::Point>
    > contours;


    std::vector<cv::Vec4i>
        hierarchy;


    cv::findContours(
        bitmap,
        contours,
        hierarchy,
        cv::RETR_LIST,
        cv::CHAIN_APPROX_SIMPLE
    );


    // =====================================================
    // Generate candidates
    // =====================================================

    std::vector<Candidate>
        candidates;


    candidates.reserve(
        contours.size()
    );


    const float minimumSize =
        3.0f * scale;


    if (!contours.empty())
    {
        if (
            contours.size() >
            static_cast<std::size_t>(
                MaximumCandidates
            )
        )
        {
            contours.resize(
                static_cast<std::size_t>(
                    MaximumCandidates
                )
            );
        }


        for (
            const std::vector<cv::Point>& contour :
            contours
        )
        {
            // ---------------------------------------------
            // Tiny contour
            // ---------------------------------------------

            if (
                contour.size() <= 2
            )
            {
                continue;
            }


            // ---------------------------------------------
            // Contour probability score
            // ---------------------------------------------

            const double score =
                contourScore(
                    probabilityImage,
                    contour
                );


            if (
                score <
                BoxThreshold
            )
            {
                continue;
            }


            // ---------------------------------------------
            // Rotated rectangle
            // ---------------------------------------------

            cv::RotatedRect rotated =
                cv::minAreaRect(
                    contour
                );


            const float maxDimension =
                std::max(
                    rotated.size.width,
                    rotated.size.height
                );


            if (
                maxDimension <
                minimumSize
            )
            {
                continue;
            }


            // ---------------------------------------------
            // Determine orientation
            //
            // Kept consistent with Tencent's implementation.
            // ---------------------------------------------

            int orientation =
                0;


            if (
                rotated.angle >= -30.0f &&
                rotated.angle <= 30.0f &&
                rotated.size.height >
                    rotated.size.width *
                    2.7f
            )
            {
                orientation =
                    1;
            }


            if (
                (
                    rotated.angle <= -60.0f ||
                    rotated.angle >= 60.0f
                ) &&
                rotated.size.width >
                    rotated.size.height *
                    2.7f
            )
            {
                orientation =
                    1;
            }


            if (
                rotated.angle <
                -30.0f
            )
            {
                rotated.angle +=
                    180.0f;
            }


            if (
                orientation == 0 &&
                rotated.angle <
                    30.0f
            )
            {
                rotated.angle +=
                    90.0f;


                std::swap(
                    rotated.size.width,
                    rotated.size.height
                );
            }


            if (
                orientation == 1 &&
                rotated.angle >=
                    60.0f
            )
            {
                rotated.angle -=
                    90.0f;


                std::swap(
                    rotated.size.width,
                    rotated.size.height
                );
            }


            // ---------------------------------------------
            // PP-OCRv5 expansion
            // ---------------------------------------------

            rotated.size.height +=
                rotated.size.width *
                (
                    EnlargeRatio -
                    1.0f
                );


            rotated.size.width *=
                EnlargeRatio;


            // ---------------------------------------------
            // Remove padding and undo scale
            // ---------------------------------------------

            rotated.center.x =
                (
                    rotated.center.x -
                    static_cast<float>(
                        widthPadding / 2
                    )
                ) /
                scale;


            rotated.center.y =
                (
                    rotated.center.y -
                    static_cast<float>(
                        heightPadding / 2
                    )
                ) /
                scale;


            rotated.size.width /=
                scale;


            rotated.size.height /=
                scale;


            // ---------------------------------------------
            // Validate
            // ---------------------------------------------

            if (
                !isFinite(
                    rotated.center.x
                ) ||
                !isFinite(
                    rotated.center.y
                ) ||
                !isFinite(
                    rotated.size.width
                ) ||
                !isFinite(
                    rotated.size.height
                )
            )
            {
                continue;
            }


            if (
                rotated.size.width <= 1.0f ||
                rotated.size.height <= 1.0f
            )
            {
                continue;
            }


            Candidate candidate;


            candidate.rotatedBox =
                rotated;


            candidate.confidence =
                clamp01(
                    static_cast<float>(
                        score
                    )
                );


            candidate.orientation =
                orientation;


            candidates.push_back(
                candidate
            );
        }
    }


    // =====================================================
    // Sort candidates by confidence
    // =====================================================

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


    // =====================================================
    // Convert PP-OCR candidates to AI detections
    // =====================================================

    for (
        const Candidate& candidate :
        candidates
    )
    {
        const cv::Rect2f rectangle =
            rotatedRectToBoundingRect(
                candidate.rotatedBox
            );


        float x =
            rectangle.x;

        float y =
            rectangle.y;

        float width =
            rectangle.width;

        float height =
            rectangle.height;


        // -------------------------------------------------
        // Clamp to original frame
        // -------------------------------------------------

        x =
            std::max(
                0.0f,
                std::min(
                    x,
                    static_cast<float>(
                        imageWidth
                    )
                )
            );


        y =
            std::max(
                0.0f,
                std::min(
                    y,
                    static_cast<float>(
                        imageHeight
                    )
                )
            );


        width =
            std::max(
                0.0f,
                std::min(
                    width,
                    static_cast<float>(
                        imageWidth
                    ) -
                    x
                )
            );


        height =
            std::max(
                0.0f,
                std::min(
                    height,
                    static_cast<float>(
                        imageHeight
                    ) -
                    y
                )
            );


        if (
            width <= 1.0f ||
            height <= 1.0f
        )
        {
            continue;
        }


        LicensePlateDetection
            detection;


        detection.confidence =
            candidate.confidence;


        detection.boundingBox.x =
            clamp01(
                x /
                static_cast<float>(
                    imageWidth
                )
            );


        detection.boundingBox.y =
            clamp01(
                y /
                static_cast<float>(
                    imageHeight
                )
            );


        detection.boundingBox.width =
            clamp01(
                width /
                static_cast<float>(
                    imageWidth
                )
            );


        detection.boundingBox.height =
            clamp01(
                height /
                static_cast<float>(
                    imageHeight
                )
            );


        plates.push_back(
            detection
        );


        if (
            plates.size() >=
            MaximumPlates
        )
        {
            break;
        }
    }


    // =====================================================
    // OpenCV fallback
    //
    // PP-OCRv5 is a text-region detector rather than a
    // vehicle-specific plate detector. On the current vehicle
    // crops it can return a weak text region while missing the
    // actual plate. Always give the lightweight plate-localization
    // fallback a chance. If it finds a strong color/shape plate
    // candidate, prefer that result; otherwise retain the
    // PP-OCRv5 result.
    // =====================================================

    {
        static bool printedFallbackMessage =
            false;

        if (!printedFallbackMessage)
        {
            printedFallbackMessage =
                true;

            std::cout
                << "LicensePlateDetector: "
                   "running OpenCV plate-localization fallback."
                << std::endl;
        }

        std::vector<
            LicensePlateDetection
        > fallbackPlates;

        if (
            detectWithOpenCVFallback(
                frame,
                fallbackPlates
            ) &&
            !fallbackPlates.empty()
        )
        {
            /*
             * The fallback is explicitly plate-shaped and operates
             * on an already detected vehicle crop. Prefer it when
             * available so a weak OCR text region cannot hide the
             * actual plate candidate.
             */
            plates =
                std::move(
                    fallbackPlates
                );

            static bool printedSuccessMessage =
                false;

            if (!printedSuccessMessage)
            {
                printedSuccessMessage =
                    true;

                std::cout
                    << "LicensePlateDetector: "
                       "OpenCV plate candidate detected."
                    << std::endl;
            }
        }
    }


    return true;
}


// =========================================================
// Detection -> AIResult
// =========================================================

bool LicensePlateDetector::detect(
    const AIFrame& frame,
    AIResult& result
)
{
    result.clear();


    std::vector<
        LicensePlateDetection
    > plates;


    if (
        !detect(
            frame,
            plates
        )
    )
    {
        return false;
    }


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
            plate.boundingBox;


        detection.trackId =
            -1;


        detection.identity.clear();


        result.detections.push_back(
            std::move(
                detection
            )
        );
    }


    result.inputWidth =
        frame.width;


    result.inputHeight =
        frame.height;


    result.inferenceSucceeded =
        true;


    return true;
}