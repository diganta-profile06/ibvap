#include "MobileStreamServer.hpp"

#include "MobileNetwork.hpp"

#include <SDL3/SDL.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    /*
     * =========================================================
     * VIDEO DECODER CONFIGURATION
     * =========================================================
     */

    /*
     * This is deliberately tiny.
     *
     * It is NOT a video buffer.
     *
     * If the decoder cannot keep up, stale compressed
     * access units are discarded and decoding resumes
     * from the next IDR frame.
     */
    constexpr std::size_t MAX_DECODE_QUEUE = 2;


    /*
     * =========================================================
     * NETWORK TELEMETRY CONFIGURATION
     * =========================================================
     */

    /*
     * Send approximately one telemetry probe per second.
     */
    constexpr auto NETWORK_PROBE_INTERVAL =
        std::chrono::seconds(1);


    /*
     * A probe is considered lost if no response arrives
     * within this period.
     *
     * This is deliberately longer than the normal probe
     * interval so a slightly delayed response is not
     * immediately counted as loss.
     */
    constexpr auto NETWORK_PROBE_TIMEOUT =
        std::chrono::milliseconds(1500);


    /*
     * =========================================================
     * EXECUTABLE DIRECTORY
     * =========================================================
     */

    std::filesystem::path
    getExecutableDirectory()
    {
        const char* basePath =
            SDL_GetBasePath();

        if (basePath != nullptr)
        {
            std::filesystem::path result(
                basePath
            );

            SDL_free(
                const_cast<char*>(basePath)
            );

            return result;
        }

        return std::filesystem::current_path();
    }


    /*
     * =========================================================
     * FILE LOADING
     * =========================================================
     */

    std::string loadTextFile(
        const std::filesystem::path& path
    )
    {
        std::ifstream file(
            path,
            std::ios::binary
        );

        if (!file)
        {
            return {};
        }

        std::ostringstream stream;

        stream << file.rdbuf();

        return stream.str();
    }


    /*
     * =========================================================
     * CLOCK
     * =========================================================
     *
     * steady_clock is used deliberately.
     *
     * RTT must not depend on the phone's wall clock or
     * the workstation's system-clock adjustments.
     */

    std::int64_t currentTimestampUs()
    {
        const auto now =
            std::chrono::time_point_cast<
                std::chrono::microseconds
            >(
                std::chrono::steady_clock::now()
            );

        return now.time_since_epoch().count();
    }


    /*
     * =========================================================
     * H.264 IDR DETECTION
     * =========================================================
     */

    /*
     * WebCodecs is configured for Annex-B.
     *
     * Detect an IDR NAL so that after deliberately
     * dropping compressed data we can restart decoding
     * only from a clean H.264 recovery point.
     */
    bool containsH264IDR(
        const std::string& data
    )
    {
        const std::size_t size =
            data.size();

        if (size < 5)
        {
            return false;
        }

        for (
            std::size_t i = 0;
            i + 3 < size;
            ++i
        )
        {
            std::size_t nalStart = 0;

            if (
                i + 4 <= size &&
                static_cast<unsigned char>(
                    data[i]
                ) == 0x00 &&
                static_cast<unsigned char>(
                    data[i + 1]
                ) == 0x00 &&
                static_cast<unsigned char>(
                    data[i + 2]
                ) == 0x00 &&
                static_cast<unsigned char>(
                    data[i + 3]
                ) == 0x01
            )
            {
                nalStart = i + 4;
            }
            else if (
                i + 3 <= size &&
                static_cast<unsigned char>(
                    data[i]
                ) == 0x00 &&
                static_cast<unsigned char>(
                    data[i + 1]
                ) == 0x00 &&
                static_cast<unsigned char>(
                    data[i + 2]
                ) == 0x01
            )
            {
                nalStart = i + 3;
            }
            else
            {
                continue;
            }

            if (nalStart >= size)
            {
                continue;
            }

            const std::uint8_t nalType =
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(
                        data[nalStart]
                    ) & 0x1F
                );

            if (nalType == 5)
            {
                return true;
            }

            i = nalStart;
        }

        return false;
    }
}


/*
 * =============================================================
 * DECODER STATE
 * =============================================================
 */

struct MobileStreamServer::DecoderState
{
    std::mutex mutex;

    std::condition_variable condition;

    std::deque<std::string> encodedQueue;

    bool stopRequested = false;


    /*
     * Protected by mutex.
     *
     * The WebSocket thread sets this when compressed
     * data was intentionally discarded.
     *
     * The decoder thread performs the actual FFmpeg
     * flush so the AVCodecContext is never manipulated
     * concurrently by two threads.
     */
    bool recoveryRequested = false;


    /*
     * While recovering, delta frames are ignored until
     * an IDR is received.
     */
    bool waitingForKeyframe = false;

    std::thread workerThread;

    std::function<void(Frame)> publishCallback;

    const AVCodec* codec = nullptr;

    AVCodecContext* context = nullptr;

    AVPacket* packet = nullptr;

    AVFrame* decodedFrame = nullptr;

    SwsContext* scaler = nullptr;

    int scalerWidth = 0;

    int scalerHeight = 0;

    AVPixelFormat scalerFormat =
        AV_PIX_FMT_NONE;


    ~DecoderState()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex
            );

            stopRequested = true;

            condition.notify_all();
        }

        if (workerThread.joinable())
        {
            workerThread.join();
        }

        if (scaler != nullptr)
        {
            sws_freeContext(
                scaler
            );

            scaler = nullptr;
        }

        if (decodedFrame != nullptr)
        {
            av_frame_free(
                &decodedFrame
            );
        }

        if (packet != nullptr)
        {
            av_packet_free(
                &packet
            );
        }

        if (context != nullptr)
        {
            avcodec_free_context(
                &context
            );
        }
    }


    bool initialize()
    {
        codec =
            avcodec_find_decoder(
                AV_CODEC_ID_H264
            );

        if (codec == nullptr)
        {
            return false;
        }

        context =
            avcodec_alloc_context3(
                codec
            );

        if (context == nullptr)
        {
            return false;
        }


        /*
         * Low-latency decoding.
         */
        context->flags |=
            AV_CODEC_FLAG_LOW_DELAY;


        /*
         * Let FFmpeg select an appropriate
         * number of worker threads.
         */
        context->thread_count = 0;


        /*
         * Slice threading avoids introducing
         * frame-level buffering here.
         */
        context->thread_type =
            FF_THREAD_SLICE;


        const int openResult =
            avcodec_open2(
                context,
                codec,
                nullptr
            );

        if (openResult < 0)
        {
            return false;
        }


        packet =
            av_packet_alloc();

        if (packet == nullptr)
        {
            return false;
        }


        decodedFrame =
            av_frame_alloc();

        if (decodedFrame == nullptr)
        {
            return false;
        }


        workerThread =
            std::thread(
                [this]()
                {
                    workerLoop();
                }
            );


        return true;
    }


    void requestRecovery()
    {
        /*
         * This function is called from the WebSocket
         * thread. It does NOT touch FFmpeg.
         */
        std::lock_guard<std::mutex> lock(
            mutex
        );

        if (stopRequested)
        {
            return;
        }

        encodedQueue.clear();

        recoveryRequested = true;

        waitingForKeyframe = true;

        condition.notify_one();
    }


    void submit(
        const std::string& encodedData
    )
    {
        if (encodedData.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(
            mutex
        );

        if (stopRequested)
        {
            return;
        }


        /*
         * If the queue is full, latency is already
         * increasing.
         *
         * Discard everything waiting and restart from
         * a clean IDR.
         */
        if (
            encodedQueue.size() >=
            MAX_DECODE_QUEUE
        )
        {
            encodedQueue.clear();

            recoveryRequested = true;

            waitingForKeyframe = true;
        }


        /*
         * During recovery, ignore arbitrary P/B frames.
         *
         * The first IDR becomes the new decoding anchor.
         */
        if (waitingForKeyframe)
        {
            if (
                !containsH264IDR(
                    encodedData
                )
            )
            {
                return;
            }

            waitingForKeyframe = false;
        }


        /*
         * Keep the compressed queue bounded even during
         * a very short burst.
         */
        if (
            encodedQueue.size() >=
            MAX_DECODE_QUEUE
        )
        {
            encodedQueue.clear();

            recoveryRequested = true;

            waitingForKeyframe = true;

            return;
        }


        encodedQueue.push_back(
            encodedData
        );

        condition.notify_one();
    }


    void workerLoop()
    {
        while (true)
        {
            std::string encodedData;

            bool flushDecoder = false;


            {
                std::unique_lock<std::mutex> lock(
                    mutex
                );

                condition.wait(
                    lock,
                    [this]()
                    {
                        return
                            stopRequested ||
                            recoveryRequested ||
                            !encodedQueue.empty();
                    }
                );


                if (stopRequested)
                {
                    return;
                }


                if (recoveryRequested)
                {
                    recoveryRequested = false;

                    flushDecoder = true;
                }


                if (encodedQueue.empty())
                {
                    /*
                     * A recovery request can intentionally
                     * wake the worker before the next IDR
                     * arrives.
                     */
                    if (flushDecoder)
                    {
                        /*
                         * FFmpeg flush happens below, outside
                         * the mutex.
                         */
                    }
                    else
                    {
                        continue;
                    }
                }
                else
                {
                    encodedData =
                        std::move(
                            encodedQueue.front()
                        );

                    encodedQueue.pop_front();
                }
            }


            /*
             * IMPORTANT:
             *
             * FFmpeg is flushed only by its own decoder
             * thread. This prevents concurrent access to
             * AVCodecContext from Crow's WebSocket thread.
             */
            if (flushDecoder)
            {
                avcodec_flush_buffers(
                    context
                );
            }


            if (encodedData.empty())
            {
                continue;
            }


            Frame newestFrame;


            if (
                decode(
                    encodedData,
                    newestFrame
                )
            )
            {
                if (publishCallback)
                {
                    publishCallback(
                        std::move(
                            newestFrame
                        )
                    );
                }
            }
        }
    }


    bool decode(
        const std::string& encodedData,
        Frame& outputFrame
    )
    {
        if (
            encodedData.empty() ||
            context == nullptr ||
            packet == nullptr ||
            decodedFrame == nullptr
        )
        {
            return false;
        }


        if (
            encodedData.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()
            )
        )
        {
            return false;
        }


        av_packet_unref(
            packet
        );


        const int allocationResult =
            av_new_packet(
                packet,
                static_cast<int>(
                    encodedData.size()
                )
            );


        if (allocationResult < 0)
        {
            return false;
        }


        std::memcpy(
            packet->data,
            encodedData.data(),
            encodedData.size()
        );


        const int sendResult =
            avcodec_send_packet(
                context,
                packet
            );


        if (
            sendResult < 0 &&
            sendResult != AVERROR(EAGAIN)
        )
        {
            return false;
        }


        bool gotFrame = false;


        while (true)
        {
            const int receiveResult =
                avcodec_receive_frame(
                    context,
                    decodedFrame
                );


            if (
                receiveResult ==
                AVERROR(EAGAIN)
            )
            {
                break;
            }


            if (
                receiveResult ==
                AVERROR_EOF
            )
            {
                break;
            }


            if (receiveResult < 0)
            {
                break;
            }


            const int width =
                decodedFrame->width;


            const int height =
                decodedFrame->height;


            const AVPixelFormat format =
                static_cast<AVPixelFormat>(
                    decodedFrame->format
                );


            if (
                width <= 0 ||
                height <= 0
            )
            {
                continue;
            }


            scaler =
                sws_getCachedContext(
                    scaler,
                    width,
                    height,
                    format,
                    width,
                    height,
                    AV_PIX_FMT_BGR24,
                    SWS_FAST_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr
                );


            if (scaler == nullptr)
            {
                return false;
            }


            scalerWidth =
                width;

            scalerHeight =
                height;

            scalerFormat =
                format;


            const std::size_t stride =
                static_cast<std::size_t>(
                    width
                ) * 3;


            const std::size_t totalBytes =
                stride *
                static_cast<std::size_t>(
                    height
                );


            auto buffer =
                std::make_shared<FrameBuffer>();


            buffer->data.resize(
                totalBytes
            );


            std::uint8_t*
                destination[] =
                {
                    buffer->data.data()
                };


            const int
                destinationStride[] =
                {
                    static_cast<int>(
                        stride
                    )
                };


            const int scaledHeight =
                sws_scale(
                    scaler,
                    decodedFrame->data,
                    decodedFrame->linesize,
                    0,
                    height,
                    destination,
                    destinationStride
                );


            if (
                scaledHeight <= 0
            )
            {
                continue;
            }


            outputFrame.buffer =
                std::move(
                    buffer
                );


            outputFrame.width =
                static_cast<std::uint32_t>(
                    width
                );


            outputFrame.height =
                static_cast<std::uint32_t>(
                    height
                );


            outputFrame.stride =
                static_cast<std::uint32_t>(
                    stride
                );


            outputFrame.format =
                PixelFormat::BGR8;


            outputFrame.timestampUs =
                currentTimestampUs();


            /*
             * If multiple decoded frames came from one
             * packet, this naturally leaves the newest one
             * in outputFrame.
             */
            gotFrame = true;
        }


        return gotFrame;
    }


    void flush()
    {
        std::lock_guard<std::mutex> lock(
            mutex
        );

        encodedQueue.clear();

        recoveryRequested = true;

        waitingForKeyframe = true;

        condition.notify_one();
    }
};


/*
 * =============================================================
 * CONSTRUCTOR / DESTRUCTOR
 * =============================================================
 */

MobileStreamServer::MobileStreamServer()
{
    const auto addresses =
        MobileNetwork::getLocalIPv4Addresses();


    /*
     * IMPORTANT:
     *
     * Do not expose every private IPv4 address.
     *
     * A Windows machine may have several private addresses:
     *
     *     192.168.0.108  -> Wi-Fi
     *     172.24.128.1   -> WSL / virtual adapter
     *     192.168.x.x    -> another adapter
     *
     * The mobile URL and QR code must use the primary
     * address selected by MobileNetwork.
     */
    const std::string primaryAddress =
        MobileNetwork::choosePrimaryAddress(
            addresses
        );


    if (!primaryAddress.empty())
    {
        m_localAddresses.push_back(
            primaryAddress
        );
    }
}


MobileStreamServer::~MobileStreamServer()
{
    stop();
}


/*
 * =============================================================
 * START
 * =============================================================
 */

bool MobileStreamServer::start(
    std::uint16_t port
)
{
    if (m_running.load())
    {
        return true;
    }


    m_port = port;


    const std::filesystem::path
        executableDirectory =
            getExecutableDirectory();


    const std::filesystem::path
        certificatePath =
            executableDirectory /
            "ibvap_mobile.crt";


    const std::filesystem::path
        privateKeyPath =
            executableDirectory /
            "ibvap_mobile.key";


    if (
        !std::filesystem::exists(
            certificatePath
        ) ||
        !std::filesystem::exists(
            privateKeyPath
        )
    )
    {
        if (m_localAddresses.empty())
        {
            m_localAddresses.push_back(
                "127.0.0.1"
            );
        }


        if (
            !MobileNetwork::generateCertificate(
                certificatePath.string(),
                privateKeyPath.string(),
                m_localAddresses
            )
        )
        {
            return false;
        }
    }


    const std::filesystem::path
        mobilePagePath =
            executableDirectory /
            "mobile" /
            "index.html";


    const std::string mobilePage =
        loadTextFile(
            mobilePagePath
        );


    if (mobilePage.empty())
    {
        return false;
    }


    auto app =
        std::make_shared<crow::SimpleApp>();


    crow::SimpleApp& crowApp =
        *app;


    CROW_ROUTE(
        crowApp,
        "/mobile"
    )
    ([mobilePage]()
    {
        crow::response response(
            mobilePage
        );


        response.set_header(
            "Content-Type",
            "text/html; charset=utf-8"
        );


        response.set_header(
            "Cache-Control",
            "no-store, no-cache, must-revalidate, max-age=0"
        );


        response.set_header(
            "Pragma",
            "no-cache"
        );


        return response;
    });


    /*
     * =========================================================
     * MOBILE PRODUCER WEBSOCKET
     * =========================================================
     *
     * Phone:
     *
     *     WebCodecs H.264
     *          |
     *          v
     *     /mobile/ws
     *
     * This is the existing native mobile-camera path.
     */
    CROW_WEBSOCKET_ROUTE(
        crowApp,
        "/mobile/ws"
    )
    .onopen(
        [this](
            crow::websocket::connection& connection
        )
        {
            std::lock_guard<std::mutex> lock(
                m_connectionStreamsMutex
            );


            m_connectionStreams[
                &connection
            ] = {};
        }
    )
    .onclose(
        [this](
            crow::websocket::connection& connection,
            const std::string&,
            uint16_t
        )
        {
            std::string streamId;


            {
                std::lock_guard<std::mutex> lock(
                    m_connectionStreamsMutex
                );


                const auto iterator =
                    m_connectionStreams.find(
                        &connection
                    );


                if (
                    iterator !=
                    m_connectionStreams.end()
                )
                {
                    streamId =
                        iterator->second;


                    m_connectionStreams.erase(
                        iterator
                    );
                }
            }


            if (!streamId.empty())
            {
                setConnected(
                    streamId,
                    false
                );


                auto decoder =
                    getOrCreateDecoder(
                        streamId
                    );


                if (decoder)
                {
                    decoder->flush();
                }


                /*
                 * Remove the decoder from the server map.
                 *
                 * The local shared_ptr keeps it alive until
                 * the flush/destructor sequence is complete.
                 */
                destroyDecoder(
                    streamId
                );
            }
        }
    )
    .onmessage(
        [this](
            crow::websocket::connection& connection,
            const std::string& data,
            bool isBinary
        )
        {
            /*
             * =================================================
             * TEXT CONTROL MESSAGES
             * =================================================
             */

            if (!isBinary)
            {
                /*
                 * ---------------------------------------------
                 * SOURCE REGISTRATION
                 * ---------------------------------------------
                 */

                if (
                    data.rfind(
                        "SOURCE:",
                        0
                    ) == 0
                )
                {
                    const std::string streamId =
                        data.substr(7);


                    if (streamId.empty())
                    {
                        return;
                    }


                    registerStream(
                        streamId
                    );


                    std::string previousStreamId;


                    {
                        std::lock_guard<std::mutex> lock(
                            m_connectionStreamsMutex
                        );


                        auto iterator =
                            m_connectionStreams.find(
                                &connection
                            );


                        if (
                            iterator !=
                            m_connectionStreams.end()
                        )
                        {
                            previousStreamId =
                                iterator->second;


                            iterator->second =
                                streamId;
                        }
                        else
                        {
                            m_connectionStreams[
                                &connection
                            ] = streamId;
                        }
                    }


                    if (
                        !previousStreamId.empty() &&
                        previousStreamId != streamId
                    )
                    {
                        setConnected(
                            previousStreamId,
                            false
                        );
                    }


                    /*
                     * A SOURCE message starts a new
                     * telemetry session for this mobile
                     * source.
                     */
                    resetNetworkTelemetry(
                        streamId
                    );


                    setConnected(
                        streamId,
                        true
                    );


                    return;
                }


                /*
                 * ---------------------------------------------
                 * NETWORK TELEMETRY PONG
                 * ---------------------------------------------
                 */

                if (
                    data.rfind(
                        "IBVAP_NET_PONG:",
                        0
                    ) == 0
                )
                {
                    std::string streamId;


                    {
                        std::lock_guard<std::mutex> lock(
                            m_connectionStreamsMutex
                        );


                        const auto iterator =
                            m_connectionStreams.find(
                                &connection
                            );


                        if (
                            iterator ==
                            m_connectionStreams.end()
                        )
                        {
                            return;
                        }


                        streamId =
                            iterator->second;
                    }


                    if (streamId.empty())
                    {
                        return;
                    }


                    handleNetworkPong(
                        streamId,
                        data
                    );


                    return;
                }


                /*
                 * Unknown text messages are ignored.
                 *
                 * This is important because text control
                 * traffic must never be interpreted as H.264.
                 */
                return;
            }


            /*
             * =================================================
             * BINARY H.264 DATA
             * =================================================
             */

            std::string streamId;


            {
                std::lock_guard<std::mutex> lock(
                    m_connectionStreamsMutex
                );


                const auto iterator =
                    m_connectionStreams.find(
                        &connection
                    );


                if (
                    iterator ==
                    m_connectionStreams.end()
                )
                {
                    return;
                }


                streamId =
                    iterator->second;
            }


            if (streamId.empty())
            {
                return;
            }


            /*
             * -------------------------------------------------
             * BROWSER / WASM VIEWER RELAY
             * -------------------------------------------------
             *
             * Forward the ORIGINAL Annex-B H.264 access unit.
             *
             * No FFmpeg decoding is performed for this relay.
             * No re-encoding is performed.
             *
             * The native decoder below continues to receive
             * exactly the same data as before.
             */
            broadcastEncodedFrame(
                streamId,
                data
            );


            /*
             * Existing H.264 native decoder path.
             */
            receiveFrame(
                streamId,
                data
            );
        }
    );


    /*
     * =========================================================
     * BROWSER / WASM VIEWER WEBSOCKET
     * =========================================================
     *
     * Browser/WASM:
     *
     *     /mobile/viewer/ws
     *
     * Registration:
     *
     *     VIEWER:source_1
     *
     * The viewer receives the original H.264 Annex-B binary
     * access units produced by the phone.
     *
     * This is deliberately a separate WebSocket map from
     * m_connectionStreams because producer and viewer
     * connections have different responsibilities.
     */
    CROW_WEBSOCKET_ROUTE(
        crowApp,
        "/mobile/viewer/ws"
    )
    .onopen(
        [this](
            crow::websocket::connection& connection
        )
        {
            std::lock_guard<std::mutex> lock(
                m_viewerConnectionsMutex
            );


            /*
             * Empty stream ID means the viewer has connected
             * but has not selected a source yet.
             */
            m_viewerConnections[
                &connection
            ] = {};
        }
    )
    .onclose(
        [this](
            crow::websocket::connection& connection,
            const std::string&,
            uint16_t
        )
        {
            std::lock_guard<std::mutex> lock(
                m_viewerConnectionsMutex
            );


            m_viewerConnections.erase(
                &connection
            );
        }
    )
    .onmessage(
        [this](
            crow::websocket::connection& connection,
            const std::string& data,
            bool isBinary
        )
        {
            /*
             * Viewer registration is a small text control
             * message. Viewer video data is server -> viewer
             * only, so binary messages from viewers are ignored.
             */
            if (isBinary)
            {
                return;
            }


            if (
                data.rfind(
                    "VIEWER:",
                    0
                ) != 0
            )
            {
                return;
            }


            const std::string streamId =
                data.substr(7);


            if (streamId.empty())
            {
                return;
            }


            /*
             * Confirm that this source is known to the
             * server before accepting the subscription.
             */
            {
                std::lock_guard<std::mutex> streamLock(
                    m_streamsMutex
                );


                if (
                    m_streams.find(
                        streamId
                    ) ==
                    m_streams.end()
                )
                {
                    return;
                }
            }


            {
                std::lock_guard<std::mutex> viewerLock(
                    m_viewerConnectionsMutex
                );


                auto iterator =
                    m_viewerConnections.find(
                        &connection
                    );


                if (
                    iterator ==
                    m_viewerConnections.end()
                )
                {
                    /*
                     * The viewer may have sent its
                     * registration before the onopen
                     * bookkeeping became visible.
                     *
                     * Create the entry safely.
                     */
                    m_viewerConnections[
                        &connection
                    ] = streamId;
                }
                else
                {
                    /*
                     * Re-registering changes the viewer's
                     * selected mobile source.
                     */
                    iterator->second =
                        streamId;
                }
            }
        }
    );


    crowApp
        .bindaddr(
            "0.0.0.0"
        )
        .port(
            m_port
        )
        .ssl_file(
            certificatePath.string(),
            privateKeyPath.string()
        )
        .multithreaded();


    {
        std::lock_guard<std::mutex> lock(
            m_appMutex
        );


        m_app =
            app;
    }


    m_running.store(
        true
    );


    /*
     * =========================================================
     * CROW SERVER THREAD
     * =========================================================
     */

    m_serverThread =
        std::thread(
            [this, app]()
            {
                app->run();


                m_running.store(
                    false
                );
            }
        );


    /*
     * =========================================================
     * NETWORK TELEMETRY THREAD
     * =========================================================
     *
     * This thread only sends tiny WebSocket control
     * messages and updates measurement state.
     *
     * It never touches FFmpeg or video frames.
     */

    m_telemetryThread =
        std::thread(
            [this]()
            {
                networkTelemetryLoop();
            }
        );


    return true;
}


/*
 * =============================================================
 * STOP
 * =============================================================
 */

void MobileStreamServer::stop() noexcept
{
    /*
     * Tell both server and telemetry threads to stop.
     */
    m_running.store(
        false
    );


    /*
     * Stop Crow.
     */
    std::shared_ptr<crow::SimpleApp> app;


    {
        std::lock_guard<std::mutex> lock(
            m_appMutex
        );


        app =
            m_app;
    }


    if (app)
    {
        app->stop();
    }


    /*
     * Stop telemetry thread BEFORE destroying stream
     * and decoder state.
     */
    if (m_telemetryThread.joinable())
    {
        m_telemetryThread.join();
    }


    /*
     * Wait for Crow's server thread.
     */
    if (m_serverThread.joinable())
    {
        m_serverThread.join();
    }


    {
        std::lock_guard<std::mutex> lock(
            m_connectionStreamsMutex
        );


        m_connectionStreams.clear();
    }


    /*
     * Remove all browser/WASM viewer registrations too.
     *
     * The actual Crow WebSocket connections are owned by
     * Crow and are shut down with the application.
     */
    {
        std::lock_guard<std::mutex> lock(
            m_viewerConnectionsMutex
        );


        m_viewerConnections.clear();
    }


    {
        std::lock_guard<std::mutex> lock(
            m_streamsMutex
        );


        for (
            auto& [id, stream] :
            m_streams
        )
        {
            if (!stream)
            {
                continue;
            }


            std::lock_guard<std::mutex> streamLock(
                stream->mutex
            );


            stream->connected =
                false;


            stream->probePending =
                false;
        }
    }


    {
        std::lock_guard<std::mutex> lock(
            m_decodersMutex
        );


        m_decoders.clear();
    }


    {
        std::lock_guard<std::mutex> lock(
            m_appMutex
        );


        m_app.reset();
    }
}


/*
 * =============================================================
 * SERVER STATE
 * =============================================================
 */

bool MobileStreamServer::isRunning()
    const noexcept
{
    return m_running.load();
}


/*
 * =============================================================
 * FRAME ACCESS
 * =============================================================
 */

bool MobileStreamServer::hasFrame(
    const std::string& streamId
) const noexcept
{
    std::shared_ptr<StreamState> stream;


    {
        std::lock_guard<std::mutex> lock(
            m_streamsMutex
        );


        const auto iterator =
            m_streams.find(
                streamId
            );


        if (
            iterator ==
            m_streams.end()
        )
        {
            return false;
        }


        stream =
            iterator->second;
    }


    if (!stream)
    {
        return false;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    return stream->latestFrame.valid();
}


bool MobileStreamServer::tryGetFrame(
    const std::string& streamId,
    Frame& frame
)
{
    std::shared_ptr<StreamState> stream;


    {
        std::lock_guard<std::mutex> lock(
            m_streamsMutex
        );


        const auto iterator =
            m_streams.find(
                streamId
            );


        if (
            iterator ==
            m_streams.end()
        )
        {
            return false;
        }


        stream =
            iterator->second;
    }


    if (!stream)
    {
        return false;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    if (!stream->latestFrame.valid())
    {
        return false;
    }


    frame =
        stream->latestFrame;


    return true;
}


/*
 * =============================================================
 * CONNECTION STATE
 * =============================================================
 */

bool MobileStreamServer::isConnected(
    const std::string& streamId
) const noexcept
{
    std::shared_ptr<StreamState> stream;


    {
        std::lock_guard<std::mutex> lock(
            m_streamsMutex
        );


        const auto iterator =
            m_streams.find(
                streamId
            );


        if (
            iterator ==
            m_streams.end()
        )
        {
            return false;
        }


        stream =
            iterator->second;
    }


    if (!stream)
    {
        return false;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    return stream->connected;
}


/*
 * =============================================================
 * NETWORK STATISTICS
 * =============================================================
 */

MobileNetworkStats
MobileStreamServer::getNetworkStats(
    const std::string& streamId
) const noexcept
{
    MobileNetworkStats stats;


    std::shared_ptr<StreamState> stream;


    {
        std::lock_guard<std::mutex> lock(
            m_streamsMutex
        );


        const auto iterator =
            m_streams.find(
                streamId
            );


        if (
            iterator ==
            m_streams.end()
        )
        {
            return stats;
        }


        stream =
            iterator->second;
    }


    if (!stream)
    {
        return stats;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    stats.connected =
        stream->connected;


    stats.measuring =
        stream->hasRttSample;


    stats.rttMs =
        stream->rttMs;


    stats.jitterMs =
        stream->jitterMs;


    stats.probesSent =
        stream->probesSent;


    stats.probesReceived =
        stream->probesReceived;


    stats.probesLost =
        stream->probesLost;


    const std::uint64_t completedProbes =
        stream->probesReceived +
        stream->probesLost;


    if (completedProbes > 0)
    {
        stats.lossPercent =
            (
                static_cast<double>(
                    stream->probesLost
                ) /
                static_cast<double>(
                    completedProbes
                )
            ) *
            100.0;
    }


    return stats;
}


/*
 * =============================================================
 * MOBILE URLS
 * =============================================================
 */

std::vector<std::string>
MobileStreamServer::localUrls(
    const std::string& streamId
) const
{
    std::vector<std::string> urls;


    for (
        const std::string& address :
        m_localAddresses
    )
    {
        if (address.empty())
        {
            continue;
        }


        urls.push_back(
            MobileNetwork::buildMobileUrl(
                address,
                m_port,
                streamId
            )
        );
    }


    return urls;
}


/*
 * =============================================================
 * STREAM MANAGEMENT
 * =============================================================
 */

void MobileStreamServer::registerStream(
    const std::string& streamId
)
{
    if (streamId.empty())
    {
        return;
    }


    getOrCreateStream(
        streamId
    );


    getOrCreateDecoder(
        streamId
    );
}


void MobileStreamServer::setConnected(
    const std::string& streamId,
    bool connected
)
{
    if (streamId.empty())
    {
        return;
    }


    const auto stream =
        getOrCreateStream(
            streamId
        );


    if (!stream)
    {
        return;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    stream->connected =
        connected;


    /*
     * If the mobile source disconnects, there is no
     * outstanding measurement that should remain pending.
     */
    if (!connected)
    {
        stream->probePending =
            false;
    }
}


/*
 * =============================================================
 * VIDEO RECEIVING
 * =============================================================
 */

void MobileStreamServer::receiveFrame(
    const std::string& streamId,
    const std::string& encodedData
)
{
    if (
        streamId.empty() ||
        encodedData.empty()
    )
    {
        return;
    }


    const auto decoder =
        getOrCreateDecoder(
            streamId
        );


    if (!decoder)
    {
        return;
    }


    /*
     * Existing H.264 decoder path.
     */
    decoder->submit(
        encodedData
    );


    const auto stream =
        getOrCreateStream(
            streamId
        );


    if (!stream)
    {
        return;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    stream->connected =
        true;
}


/*
 * =============================================================
 * BROWSER / WASM H.264 RELAY
 * =============================================================
 *
 * Sends the exact H.264 Annex-B data received from the phone
 * to every browser/WASM viewer registered for the same source.
 *
 * There is deliberately no FFmpeg work here.
 *
 * Phone:
 *
 *     H.264
 *       |
 *       +------> native FFmpeg decoder
 *       |
 *       +------> browser/WASM viewer
 *
 * This allows the native application and browser build to
 * consume the same mobile stream independently.
 */

void MobileStreamServer::broadcastEncodedFrame(
    const std::string& streamId,
    const std::string& encodedData
)
{
    if (
        streamId.empty() ||
        encodedData.empty()
    )
    {
        return;
    }


    /*
     * Keep the viewer map protected while sending.
     *
     * This is consistent with the existing WebSocket handling
     * in the server and prevents a connection from disappearing
     * from the map while it is being used.
     */
    std::lock_guard<std::mutex> lock(
        m_viewerConnectionsMutex
    );


    for (
        const auto& [connection, viewerStreamId] :
        m_viewerConnections
    )
    {
        if (
            connection == nullptr ||
            viewerStreamId != streamId
        )
        {
            continue;
        }


        try
        {
            /*
             * IMPORTANT:
             *
             * This is binary WebSocket data.
             *
             * The original Annex-B H.264 bytes are sent without
             * conversion or re-encoding.
             */
            connection->send_binary(
                encodedData
            );
        }
        catch (...)
        {
            /*
             * A viewer may disappear while the server is
             * transmitting.
             *
             * Crow will invoke the WebSocket close handler,
             * which removes the connection from
             * m_viewerConnections.
             *
             * Do not allow one disconnected viewer to break
             * delivery to the remaining viewers.
             */
        }
    }
}


/*
 * =============================================================
 * DECODED FRAME PUBLICATION
 * =============================================================
 */

void MobileStreamServer::publishDecodedFrame(
    const std::string& streamId,
    Frame frame
)
{
    if (
        streamId.empty() ||
        !frame.valid()
    )
    {
        return;
    }


    const auto stream =
        getOrCreateStream(
            streamId
        );


    if (!stream)
    {
        return;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    ++stream->sequence;


    frame.sequence =
        stream->sequence;


    /*
     * Latest frame only.
     *
     * There is deliberately no display queue.
     */
    stream->latestFrame =
        std::move(
            frame
        );


    stream->connected =
        true;
}


/*
 * =============================================================
 * STREAM LOOKUP
 * =============================================================
 */

std::shared_ptr<
    MobileStreamServer::StreamState
>
MobileStreamServer::getOrCreateStream(
    const std::string& streamId
)
{
    if (streamId.empty())
    {
        return nullptr;
    }


    std::lock_guard<std::mutex> lock(
        m_streamsMutex
    );


    const auto iterator =
        m_streams.find(
            streamId
        );


    if (
        iterator !=
        m_streams.end()
    )
    {
        return iterator->second;
    }


    auto stream =
        std::make_shared<StreamState>();


    m_streams.emplace(
        streamId,
        stream
    );


    return stream;
}


/*
 * =============================================================
 * DECODER LOOKUP
 * =============================================================
 */

std::shared_ptr<
    MobileStreamServer::DecoderState
>
MobileStreamServer::getOrCreateDecoder(
    const std::string& streamId
)
{
    if (streamId.empty())
    {
        return nullptr;
    }


    std::lock_guard<std::mutex> lock(
        m_decodersMutex
    );


    const auto iterator =
        m_decoders.find(
            streamId
        );


    if (
        iterator !=
        m_decoders.end()
    )
    {
        return iterator->second;
    }


    auto decoder =
        std::make_shared<DecoderState>();


    decoder->publishCallback =
        [this, streamId](Frame frame)
        {
            publishDecodedFrame(
                streamId,
                std::move(
                    frame
                )
            );
        };


    if (!decoder->initialize())
    {
        return nullptr;
    }


    m_decoders.emplace(
        streamId,
        decoder
    );


    return decoder;
}


/*
 * =============================================================
 * DECODER DESTRUCTION
 * =============================================================
 */

void MobileStreamServer::destroyDecoder(
    const std::string& streamId
)
{
    std::shared_ptr<DecoderState> decoder;


    {
        std::lock_guard<std::mutex> lock(
            m_decodersMutex
        );


        const auto iterator =
            m_decoders.find(
                streamId
            );


        if (
            iterator ==
            m_decoders.end()
        )
        {
            return;
        }


        decoder =
            iterator->second;


        m_decoders.erase(
            iterator
        );
    }


    /*
     * Keep the local shared_ptr alive until this function
     * returns. The decoder destructor joins its worker.
     */
    decoder.reset();
}


/*
 * =============================================================
 * NETWORK TELEMETRY RESET
 * =============================================================
 */

void MobileStreamServer::resetNetworkTelemetry(
    const std::string& streamId
)
{
    if (streamId.empty())
    {
        return;
    }


    const auto stream =
        getOrCreateStream(
            streamId
        );


    if (!stream)
    {
        return;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    /*
     * Every new SOURCE registration starts a fresh
     * measurement session.
     */
    stream->nextProbeSequence = 1;

    stream->probesSent = 0;

    stream->probesReceived = 0;

    stream->probesLost = 0;

    stream->probePending = false;

    stream->pendingProbeSequence = 0;

    stream->pendingProbeTimestampUs = 0;

    stream->rttMs = 0.0;

    stream->jitterMs = 0.0;

    stream->hasRttSample = false;

    stream->previousRttMs = 0.0;
}


/*
 * =============================================================
 * SEND NETWORK PROBE
 * =============================================================
 */

void MobileStreamServer::sendNetworkProbe(
    const std::string& streamId
)
{
    if (streamId.empty())
    {
        return;
    }


    const auto stream =
        getOrCreateStream(
            streamId
        );


    if (!stream)
    {
        return;
    }


    crow::websocket::connection*
        connection = nullptr;


    std::uint64_t sequence = 0;

    std::int64_t timestampUs = 0;


    {
        std::lock_guard<std::mutex> streamLock(
            stream->mutex
        );


        if (!stream->connected)
        {
            return;
        }


        const std::int64_t nowUs =
            currentTimestampUs();


        /*
         * If an earlier probe is still pending and has
         * exceeded the timeout, count exactly that probe
         * as lost.
         */
        if (
            stream->probePending &&
            (
                nowUs -
                stream->pendingProbeTimestampUs
            ) >=
                std::chrono::duration_cast<
                    std::chrono::microseconds
                >(
                    NETWORK_PROBE_TIMEOUT
                ).count()
        )
        {
            ++stream->probesLost;

            stream->probePending =
                false;
        }


        /*
         * Never put multiple telemetry probes in flight
         * for one mobile source.
         */
        if (stream->probePending)
        {
            return;
        }


        sequence =
            stream->nextProbeSequence++;


        timestampUs =
            nowUs;


        stream->pendingProbeSequence =
            sequence;


        stream->pendingProbeTimestampUs =
            timestampUs;


        stream->probePending =
            true;


        /*
         * Do not count the probe as sent until a
         * corresponding WebSocket connection is found.
         */
    }


    /*
     * Locate the WebSocket belonging to this source.
     */
    {
        std::lock_guard<std::mutex> connectionLock(
            m_connectionStreamsMutex
        );


        for (
            const auto& [candidate, candidateStreamId] :
            m_connectionStreams
        )
        {
            if (
                candidateStreamId ==
                streamId
            )
            {
                connection =
                    candidate;

                break;
            }
        }


        if (connection == nullptr)
        {
            /*
             * No connection exists anymore.
             *
             * Remove the pending state without counting
             * a probe that was never actually transmitted.
             */
            std::lock_guard<std::mutex> streamLock(
                stream->mutex
            );


            stream->probePending =
                false;


            return;
        }


        const std::string message =
            "IBVAP_NET_PING:" +
            std::to_string(
                sequence
            ) +
            ":" +
            std::to_string(
                timestampUs
            );


        try
        {
            connection->send_text(
                message
            );


            std::lock_guard<std::mutex> streamLock(
                stream->mutex
            );


            ++stream->probesSent;
        }
        catch (...)
        {
            /*
             * If Crow reports an exception while sending,
             * don't leave a permanently pending probe.
             */
            std::lock_guard<std::mutex> streamLock(
                stream->mutex
            );


            stream->probePending =
                false;
        }
    }
}


/*
 * =============================================================
 * HANDLE NETWORK PONG
 * =============================================================
 */

void MobileStreamServer::handleNetworkPong(
    const std::string& streamId,
    const std::string& message
)
{
    constexpr std::string_view prefix =
        "IBVAP_NET_PONG:";


    if (
        message.rfind(
            prefix,
            0
        ) != 0
    )
    {
        return;
    }


    const std::string payload =
        message.substr(
            prefix.size()
        );


    /*
     * Expected:
     *
     * <sequence>:<serverTimestampUs>
     */
    const std::size_t separator =
        payload.find(':');


    if (
        separator ==
        std::string::npos
    )
    {
        return;
    }


    const std::string sequenceText =
        payload.substr(
            0,
            separator
        );


    const std::string timestampText =
        payload.substr(
            separator + 1
        );


    std::uint64_t sequence = 0;

    std::int64_t timestampUs = 0;


    try
    {
        sequence =
            std::stoull(
                sequenceText
            );


        timestampUs =
            std::stoll(
                timestampText
            );
    }
    catch (...)
    {
        return;
    }


    const auto stream =
        getOrCreateStream(
            streamId
        );


    if (!stream)
    {
        return;
    }


    std::lock_guard<std::mutex> lock(
        stream->mutex
    );


    /*
     * Ignore responses that don't correspond to the
     * currently outstanding probe.
     */
    if (!stream->probePending)
    {
        return;
    }


    if (
        sequence !=
        stream->pendingProbeSequence
    )
    {
        return;
    }


    /*
     * We use the server timestamp echoed by the browser
     * to identify the exact probe, but the actual RTT is
     * measured entirely with the workstation's monotonic
     * clock.
     */
    if (timestampUs !=
        stream->pendingProbeTimestampUs)
    {
        return;
    }


    const std::int64_t nowUs =
        currentTimestampUs();


    const std::int64_t elapsedUs =
        nowUs -
        stream->pendingProbeTimestampUs;


    if (elapsedUs < 0)
    {
        /*
         * Should not happen with steady_clock, but reject
         * impossible measurements.
         */
        stream->probePending =
            false;

        return;
    }


    const double rttMs =
        static_cast<double>(
            elapsedUs
        ) /
        1000.0;


    /*
     * =========================================================
     * JITTER
     * =========================================================
     *
     * Smoothed absolute difference between consecutive RTT
     * measurements.
     *
     * This keeps the displayed number stable instead of
     * jumping wildly because of one sample.
     */
    if (stream->hasRttSample)
    {
        const double difference =
            std::abs(
                rttMs -
                stream->previousRttMs
            );


        stream->jitterMs =
            (
                stream->jitterMs *
                0.75
            ) +
            (
                difference *
                0.25
            );
    }
    else
    {
        stream->jitterMs =
            0.0;


        stream->hasRttSample =
            true;
    }


    stream->previousRttMs =
        rttMs;


    stream->rttMs =
        rttMs;


    ++stream->probesReceived;


    stream->probePending =
        false;
}


/*
 * =============================================================
 * NETWORK TELEMETRY LOOP
 * =============================================================
 */

void MobileStreamServer::networkTelemetryLoop()
{
    /*
     * The first probe is sent after approximately one
     * interval rather than immediately during server start.
     */
    while (m_running.load())
    {
        std::this_thread::sleep_for(
            NETWORK_PROBE_INTERVAL
        );


        if (!m_running.load())
        {
            break;
        }


        /*
         * Take a snapshot of stream IDs.
         *
         * Do not hold m_streamsMutex while sending
         * WebSocket messages.
         */
        std::vector<std::string> streamIds;


        {
            std::lock_guard<std::mutex> lock(
                m_streamsMutex
            );


            streamIds.reserve(
                m_streams.size()
            );


            for (
                const auto& [streamId, stream] :
                m_streams
            )
            {
                if (!stream)
                {
                    continue;
                }


                bool connected = false;


                {
                    std::lock_guard<std::mutex> streamLock(
                        stream->mutex
                    );


                    connected =
                        stream->connected;
                }


                if (connected)
                {
                    streamIds.push_back(
                        streamId
                    );
                }
            }
        }


        /*
         * Send one probe to every connected mobile source.
         */
        for (
            const std::string& streamId :
            streamIds
        )
        {
            if (!m_running.load())
            {
                break;
            }


            sendNetworkProbe(
                streamId
            );
        }
    }
}