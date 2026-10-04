#pragma once

#include "../video/Frame.hpp"

#include <crow.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


/*
 * =========================================================
 * MOBILE NETWORK TELEMETRY
 * =========================================================
 *
 * These measurements are generated from the dedicated
 * WebSocket telemetry probes between the mobile browser
 * and the IBVAP workstation.
 *
 * They do NOT measure H.264 decoder drops.
 *
 * lossPercent represents telemetry-probe loss.
 */

struct MobileNetworkStats
{
    bool connected = false;

    /*
     * True after at least one telemetry response has
     * successfully completed.
     */
    bool measuring = false;

    /*
     * Round-trip time in milliseconds.
     */
    double rttMs = 0.0;

    /*
     * Smoothed RTT variation in milliseconds.
     */
    double jitterMs = 0.0;

    /*
     * Percentage of telemetry probes that were considered
     * lost.
     */
    double lossPercent = 0.0;

    std::uint64_t probesSent = 0;

    std::uint64_t probesReceived = 0;

    std::uint64_t probesLost = 0;
};


class MobileStreamServer
{
public:
    MobileStreamServer();

    ~MobileStreamServer();


    MobileStreamServer(
        const MobileStreamServer&
    ) = delete;


    MobileStreamServer& operator=(
        const MobileStreamServer&
    ) = delete;


    /*
     * =====================================================
     * SERVER
     * =====================================================
     */

    bool start(
        std::uint16_t port
    );


    void stop() noexcept;


    bool isRunning() const noexcept;


    /*
     * =====================================================
     * VIDEO FRAME ACCESS
     * =====================================================
     */

    bool hasFrame(
        const std::string& streamId
    ) const noexcept;


    bool tryGetFrame(
        const std::string& streamId,
        Frame& frame
    );


    bool isConnected(
        const std::string& streamId
    ) const noexcept;


    /*
     * =====================================================
     * MOBILE NETWORK TELEMETRY
     * =====================================================
     */

    MobileNetworkStats getNetworkStats(
        const std::string& streamId
    ) const noexcept;


    /*
     * =====================================================
     * MOBILE URLS
     * =====================================================
     */

    std::vector<std::string>
    localUrls(
        const std::string& streamId
    ) const;


private:

    /*
     * =====================================================
     * DECODER
     * =====================================================
     */

    struct DecoderState;


    /*
     * =====================================================
     * STREAM STATE
     * =====================================================
     */

    struct StreamState
    {
        mutable std::mutex mutex;

        Frame latestFrame;

        bool connected = false;

        std::uint64_t sequence = 0;


        /*
         * =================================================
         * NETWORK TELEMETRY STATE
         * =================================================
         */

        /*
         * Sequence number assigned to the next probe.
         */
        std::uint64_t nextProbeSequence = 1;


        /*
         * Number of probes sent to this mobile source.
         */
        std::uint64_t probesSent = 0;


        /*
         * Number of valid probe responses received.
         */
        std::uint64_t probesReceived = 0;


        /*
         * Number of probes that timed out.
         */
        std::uint64_t probesLost = 0;


        /*
         * There is at most one outstanding probe per
         * mobile stream.
         */
        bool probePending = false;


        /*
         * Sequence number of the outstanding probe.
         */
        std::uint64_t pendingProbeSequence = 0;


        /*
         * Server-side timestamp at which the outstanding
         * probe was transmitted.
         *
         * Stored in steady-clock microseconds.
         */
        std::int64_t pendingProbeTimestampUs = 0;


        /*
         * Most recently measured RTT.
         */
        double rttMs = 0.0;


        /*
         * Smoothed jitter.
         */
        double jitterMs = 0.0;


        /*
         * Indicates whether a previous RTT sample exists.
         */
        bool hasRttSample = false;


        /*
         * Previous RTT sample used for jitter calculation.
         */
        double previousRttMs = 0.0;
    };


private:

    /*
     * =====================================================
     * STREAM MANAGEMENT
     * =====================================================
     */

    void registerStream(
        const std::string& streamId
    );


    void setConnected(
        const std::string& streamId,
        bool connected
    );


    /*
     * =====================================================
     * VIDEO DATA
     * =====================================================
     */

    /*
     * Sends encoded H.264 data to the native decoder.
     */
    void receiveFrame(
        const std::string& streamId,
        const std::string& encodedData
    );


    /*
     * Relays the original encoded H.264 access unit to
     * browser/WASM viewers registered for this source.
     *
     * This does NOT decode or re-encode the video.
     */
    void broadcastEncodedFrame(
        const std::string& streamId,
        const std::string& encodedData
    );


    /*
     * Publishes the decoded native frame.
     */
    void publishDecodedFrame(
        const std::string& streamId,
        Frame frame
    );


    /*
     * =====================================================
     * STREAM LOOKUP
     * =====================================================
     */

    std::shared_ptr<StreamState>
    getOrCreateStream(
        const std::string& streamId
    );


    std::shared_ptr<DecoderState>
    getOrCreateDecoder(
        const std::string& streamId
    );


    void destroyDecoder(
        const std::string& streamId
    );


    /*
     * =====================================================
     * NETWORK TELEMETRY
     * =====================================================
     */

    /*
     * Runs independently from the Crow HTTP/WebSocket
     * server thread.
     *
     * Approximately one probe is sent per second for
     * every connected mobile source.
     */
    void networkTelemetryLoop();


    /*
     * Sends one telemetry probe to the mobile source.
     */
    void sendNetworkProbe(
        const std::string& streamId
    );


    /*
     * Processes:
     *
     * IBVAP_NET_PONG:<sequence>:<timestamp>
     *
     * received from the mobile browser.
     */
    void handleNetworkPong(
        const std::string& streamId,
        const std::string& message
    );


    /*
     * Resets telemetry measurements when a mobile source
     * establishes a new WebSocket session.
     */
    void resetNetworkTelemetry(
        const std::string& streamId
    );


private:

    /*
     * =====================================================
     * VIDEO STREAMS
     * =====================================================
     */

    std::unordered_map<
        std::string,
        std::shared_ptr<StreamState>
    > m_streams;


    mutable std::mutex m_streamsMutex;


    /*
     * =====================================================
     * DECODERS
     * =====================================================
     */

    std::unordered_map<
        std::string,
        std::shared_ptr<DecoderState>
    > m_decoders;


    mutable std::mutex m_decodersMutex;


    /*
     * =====================================================
     * SERVER STATE
     * =====================================================
     */

    std::atomic<bool> m_running = false;


    std::uint16_t m_port = 0;


    /*
     * Crow server thread.
     */
    std::thread m_serverThread;


    /*
     * Network telemetry thread.
     */
    std::thread m_telemetryThread;


    std::shared_ptr<crow::SimpleApp> m_app;


    std::mutex m_appMutex;


    /*
     * Local network addresses used to generate mobile URLs.
     */
    std::vector<std::string> m_localAddresses;


    /*
     * =====================================================
     * MOBILE PHONE WEBSOCKET CONNECTIONS
     * =====================================================
     *
     * Maps a Crow WebSocket connection to its registered
     * mobile source ID.
     *
     * Example:
     *
     * connection A -> source_1
     * connection B -> source_2
     *
     * These connections are the actual mobile-camera
     * producers.
     */

    std::unordered_map<
        crow::websocket::connection*,
        std::string
    > m_connectionStreams;


    std::mutex m_connectionStreamsMutex;


    /*
     * =====================================================
     * WASM / BROWSER VIEWER CONNECTIONS
     * =====================================================
     *
     * Maps a browser viewer WebSocket connection to the
     * mobile source whose encoded H.264 stream it wants.
     *
     * Example:
     *
     * browser A -> source_1
     * browser B -> source_2
     *
     * These connections are separate from the physical
     * mobile-camera connections above.
     *
     * This separation is important because telemetry
     * probes must only be sent to the actual mobile
     * producer.
     */

    std::unordered_map<
        crow::websocket::connection*,
        std::string
    > m_viewerConnections;


    std::mutex m_viewerConnectionsMutex;
};