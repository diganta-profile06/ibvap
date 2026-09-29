#include "WebVideoSource.hpp"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include <cstdint>
#include <utility>


/*
 * ============================================================
 * Browser video bridge
 * ============================================================
 *
 * Source types:
 *
 * 1 = Laptop Camera
 * 2 = Local / Pre-recorded Video
 * 3 = Mobile Stream
 * 4 = RTSP
 *
 * Mobile:
 *
 * Phone
 *   -> WebCodecs H.264
 *   -> native MobileStreamServer
 *   -> /mobile/viewer/ws
 *   -> browser WebSocket
 *   -> browser WebCodecs VideoDecoder
 *   -> Canvas
 *   -> RGBA ImageData
 *   -> C++ Frame
 *
 * ============================================================
 */


/*
 * ============================================================
 * Start / restart browser source
 * ============================================================
 */

EM_JS(
    int,
    ibvap_web_start_source,
    (
        const char* sourceId,
        int sourceType,
        const char* sourceUrl
    ),
    {
        if (
            !Module.IBVAPVideoSources
        )
        {
            Module.IBVAPVideoSources = {};
        }

        const id =
            UTF8ToString(sourceId);

        const url =
            UTF8ToString(sourceUrl);

        let entry =
            Module.IBVAPVideoSources[id];


        /*
         * ----------------------------------------------------
         * Existing source
         * ----------------------------------------------------
         */

        if (
            entry
        )
        {
            /*
             * Replay pre-recorded video.
             */
            if (
                sourceType === 2 &&
                entry.video
            )
            {
                entry.error =
                    false;

                entry.started =
                    true;

                entry.ready =
                    false;

                try
                {
                    entry.video.currentTime =
                        0;
                }
                catch (e)
                {
                }

                const playPromise =
                    entry.video.play();

                if (
                    playPromise &&
                    playPromise.catch
                )
                {
                    playPromise.catch(
                        function(error)
                        {
                            console.warn(
                                "IBVAP replay play:",
                                error
                            );
                        }
                    );
                }

                entry.width =
                    entry.video.videoWidth;

                entry.height =
                    entry.video.videoHeight;

                if (
                    entry.width > 0 &&
                    entry.height > 0
                )
                {
                    entry.ready =
                        true;
                }

                return 1;
            }


            /*
             * Existing camera source.
             */
            if (
                sourceType === 1 &&
                entry.video &&
                entry.stream
            )
            {
                entry.started =
                    true;

                entry.error =
                    false;

                return 1;
            }


            /*
             * Existing mobile source.
             *
             * Recreate its viewer connection and decoder.
             */
            if (
                sourceType === 3 &&
                entry.mobile
            )
            {
                if (
                    entry.socket
                )
                {
                    try
                    {
                        entry.socket.onopen =
                            null;

                        entry.socket.onmessage =
                            null;

                        entry.socket.onerror =
                            null;

                        entry.socket.onclose =
                            null;

                        entry.socket.close();
                    }
                    catch (e)
                    {
                    }

                    entry.socket =
                        null;
                }

                if (
                    entry.decoder
                )
                {
                    try
                    {
                        entry.decoder.close();
                    }
                    catch (e)
                    {
                    }

                    entry.decoder =
                        null;
                }

                entry.generation =
                    (entry.generation || 0) + 1;

                entry.started =
                    false;

                entry.ready =
                    false;

                entry.error =
                    false;

                entry.width =
                    0;

                entry.height =
                    0;

                entry.lastImageData =
                    null;

                entry.decodeTimestamp =
                    0;
            }
        }


        /*
         * ----------------------------------------------------
         * Create new source entry.
         * ----------------------------------------------------
         */

        if (!entry)
        {
            entry =
            {
                id: id,
                type: sourceType,
                url: url,

                video: null,
                canvas: null,
                context: null,

                stream: null,

                mobile: false,
                socket: null,
                decoder: null,

                generation: 0,

                width: 0,
                height: 0,

                started: false,
                ready: false,
                error: false,

                lastImageData: null,

                decodeTimestamp: 0
            };

            Module.IBVAPVideoSources[id] =
                entry;
        }


        /*
         * ----------------------------------------------------
         * Laptop Camera
         * ----------------------------------------------------
         */

        if (
            sourceType === 1
        )
        {
            if (
                !navigator.mediaDevices ||
                !navigator.mediaDevices.getUserMedia
            )
            {
                entry.error =
                    true;

                return -1;
            }

            const video =
                document.createElement(
                    "video"
                );

            video.autoplay =
                true;

            video.muted =
                true;

            video.playsInline =
                true;

            video.setAttribute(
                "playsinline",
                ""
            );

            video.style.display =
                "none";

            document.body.appendChild(
                video
            );

            entry.video =
                video;

            entry.started =
                true;

            entry.ready =
                false;

            entry.error =
                false;

            navigator.mediaDevices
                .getUserMedia(
                {
                    video:
                    {
                        facingMode:
                            "user"
                    },

                    audio:
                        false
                })
                .then(
                    function(stream)
                    {
                        entry.stream =
                            stream;

                        video.srcObject =
                            stream;

                        video.onloadedmetadata =
                            function()
                            {
                                entry.width =
                                    video.videoWidth;

                                entry.height =
                                    video.videoHeight;

                                entry.ready =
                                    (
                                        entry.width > 0 &&
                                        entry.height > 0
                                    );
                            };

                        video.onloadeddata =
                            function()
                            {
                                entry.width =
                                    video.videoWidth;

                                entry.height =
                                    video.videoHeight;

                                entry.ready =
                                    (
                                        entry.width > 0 &&
                                        entry.height > 0
                                    );
                            };

                        video.play()
                            .then(
                                function()
                                {
                                    entry.width =
                                        video.videoWidth;

                                    entry.height =
                                        video.videoHeight;

                                    entry.ready =
                                        (
                                            entry.width > 0 &&
                                            entry.height > 0
                                        );
                                }
                            )
                            .catch(
                                function(error)
                                {
                                    console.warn(
                                        "IBVAP camera play:",
                                        error
                                    );
                                }
                            );
                    }
                )
                .catch(
                    function(error)
                    {
                        console.error(
                            "IBVAP camera error:",
                            error
                        );

                        entry.error =
                            true;

                        entry.started =
                            false;

                        entry.ready =
                            false;
                    }
                );

            return 1;
        }


        /*
         * ----------------------------------------------------
         * Pre-recorded Local Video
         * ----------------------------------------------------
         */

        if (
            sourceType === 2
        )
        {
            if (
                !url
            )
            {
                console.error(
                    "IBVAP local video: empty URL"
                );

                entry.error =
                    true;

                return -1;
            }

            const video =
                document.createElement(
                    "video"
                );

            video.autoplay =
                true;

            video.muted =
                true;

            video.playsInline =
                true;

            video.setAttribute(
                "playsinline",
                ""
            );

            video.controls =
                false;

            video.preload =
                "auto";

            video.style.display =
                "none";

            document.body.appendChild(
                video
            );

            entry.video =
                video;

            entry.started =
                true;

            entry.ready =
                false;

            entry.error =
                false;

            video.onloadedmetadata =
                function()
                {
                    entry.width =
                        video.videoWidth;

                    entry.height =
                        video.videoHeight;

                    entry.ready =
                        (
                            entry.width > 0 &&
                            entry.height > 0
                        );
                };

            video.onloadeddata =
                function()
                {
                    entry.width =
                        video.videoWidth;

                    entry.height =
                        video.videoHeight;

                    entry.ready =
                        (
                            entry.width > 0 &&
                            entry.height > 0
                        );
                };

            video.oncanplay =
                function()
                {
                    entry.width =
                        video.videoWidth;

                    entry.height =
                        video.videoHeight;

                    entry.ready =
                        (
                            entry.width > 0 &&
                            entry.height > 0
                        );
                };

            video.onerror =
                function()
                {
                    console.error(
                        "IBVAP local video error:",
                        video.error
                    );

                    entry.error =
                        true;

                    entry.ready =
                        false;
                };

            video.onended =
                function()
                {
                    /*
                     * Keep the source alive so the
                     * C++ Replay button can restart it.
                     */
                    entry.ready =
                        true;
                };

            video.src =
                url;

            video.load();

            video.play()
                .then(
                    function()
                    {
                        entry.width =
                            video.videoWidth;

                        entry.height =
                            video.videoHeight;

                        entry.ready =
                            (
                                entry.width > 0 &&
                                entry.height > 0
                            );
                    }
                )
                .catch(
                    function(error)
                    {
                        console.warn(
                            "IBVAP video play:",
                            error
                        );
                    }
                );

            return 1;
        }


        /*
         * ----------------------------------------------------
         * Mobile Stream
         * ----------------------------------------------------
         *
         * Native server route:
         *
         *     /mobile/viewer/ws
         *
         * Registration:
         *
         *     VIEWER:source_1
         *
         * Binary payload:
         *
         *     Annex-B H.264
         *
         * The receiver detects NAL type 5 (IDR) and marks
         * that EncodedVideoChunk as a WebCodecs key frame.
         * ----------------------------------------------------
         */

        if (
            sourceType === 3
        )
        {
            if (
                typeof VideoDecoder ===
                    "undefined"
            )
            {
                console.error(
                    "IBVAP mobile: WebCodecs VideoDecoder is not available"
                );

                entry.error =
                    true;

                return -1;
            }


            if (
                typeof EncodedVideoChunk ===
                    "undefined"
            )
            {
                console.error(
                    "IBVAP mobile: EncodedVideoChunk is not available"
                );

                entry.error =
                    true;

                return -1;
            }


            if (
                typeof WebSocket ===
                    "undefined"
            )
            {
                console.error(
                    "IBVAP mobile: WebSocket is not available"
                );

                entry.error =
                    true;

                return -1;
            }


            /*
             * Create the canvas bridge.
             */
            if (
                !entry.canvas
            )
            {
                entry.canvas =
                    document.createElement(
                        "canvas"
                    );

                entry.canvas.style.display =
                    "none";

                document.body.appendChild(
                    entry.canvas
                );
            }


            entry.mobile =
                true;

            entry.started =
                true;

            entry.ready =
                false;

            entry.error =
                false;

            entry.width =
                0;

            entry.height =
                0;

            entry.lastImageData =
                null;

            entry.decodeTimestamp =
                0;

            entry.generation =
                (entry.generation || 0) + 1;

            const generation =
                entry.generation;


            /*
             * ------------------------------------------------
             * Determine viewer WebSocket URL.
             * ------------------------------------------------
             *
             * IMPORTANT:
             *
             * The WASM application may be served from:
             *
             *     https://192.168.0.108:8080
             *
             * while the native IBVAP mobile server listens on:
             *
             *     https://192.168.0.108:8443
             *
             * Therefore window.location.host MUST NOT be used
             * here.
             *
             * The browser hostname is reused, but the native
             * mobile server port 8443 is selected explicitly.
             *
             * Example:
             *
             *     WASM:
             *     https://192.168.0.108:8080/IBVAP.html
             *
             *     Viewer:
             *     wss://192.168.0.108:8443/mobile/viewer/ws
             * ------------------------------------------------
             */

            let websocketUrl =
                "";

            if (
                typeof window !==
                    "undefined" &&
                window.location
            )
            {
                const hostname =
                    window.location.hostname;

                if (
                    hostname &&
                    hostname !== "null"
                )
                {
                    websocketUrl =
                        "wss://" +
                        hostname +
                        ":8443" +
                        "/mobile/viewer/ws";
                }
            }


            if (
                !websocketUrl
            )
            {
                console.error(
                    "IBVAP mobile: unable to determine WebSocket URL"
                );

                entry.error =
                    true;

                entry.started =
                    false;

                return -1;
            }


            /*
             * ------------------------------------------------
             * Helper:
             *
             * Detect whether an Annex-B H.264 access unit
             * contains an IDR NAL unit.
             *
             * H.264 NAL types:
             *
             *   1 = non-IDR slice
             *   5 = IDR slice
             *   7 = SPS
             *   8 = PPS
             * ------------------------------------------------
             */

            const containsIdr =
                function(buffer)
                {
                    if (
                        !buffer ||
                        buffer.byteLength < 5
                    )
                    {
                        return false;
                    }

                    const bytes =
                        new Uint8Array(
                            buffer
                        );

                    const length =
                        bytes.length;

                    let index =
                        0;

                    while (
                        index + 4 <
                        length
                    )
                    {
                        /*
                         * Find Annex-B start code.
                         */
                        let startCodeLength =
                            0;

                        if (
                            index + 3 <
                                length &&
                            bytes[index] === 0x00 &&
                            bytes[index + 1] === 0x00 &&
                            bytes[index + 2] === 0x01
                        )
                        {
                            startCodeLength =
                                3;
                        }
                        else if (
                            index + 4 <
                                length &&
                            bytes[index] === 0x00 &&
                            bytes[index + 1] === 0x00 &&
                            bytes[index + 2] === 0x00 &&
                            bytes[index + 3] === 0x01
                        )
                        {
                            startCodeLength =
                                4;
                        }

                        if (
                            startCodeLength === 0
                        )
                        {
                            ++index;
                            continue;
                        }


                        const nalIndex =
                            index +
                            startCodeLength;

                        if (
                            nalIndex >= length
                        )
                        {
                            break;
                        }


                        const nalType =
                            bytes[nalIndex] &
                            0x1F;

                        if (
                            nalType === 5
                        )
                        {
                            return true;
                        }


                        index =
                            nalIndex + 1;
                    }

                    return false;
                };


            /*
             * ------------------------------------------------
             * WebCodecs decoder.
             * ------------------------------------------------
             */

            let decoder =
                null;

            try
            {
                decoder =
                    new VideoDecoder(
                    {
                        output:
                            function(videoFrame)
                            {
                                if (
                                    entry.generation !==
                                        generation ||
                                    !entry.mobile ||
                                    !entry.started
                                )
                                {
                                    try
                                    {
                                        videoFrame.close();
                                    }
                                    catch (e)
                                    {
                                    }

                                    return;
                                }


                                const width =
                                    videoFrame.displayWidth ||
                                    videoFrame.codedWidth;

                                const height =
                                    videoFrame.displayHeight ||
                                    videoFrame.codedHeight;

                                if (
                                    width <= 0 ||
                                    height <= 0
                                )
                                {
                                    try
                                    {
                                        videoFrame.close();
                                    }
                                    catch (e)
                                    {
                                    }

                                    return;
                                }


                                if (
                                    !entry.canvas
                                )
                                {
                                    entry.canvas =
                                        document.createElement(
                                            "canvas"
                                        );

                                    entry.canvas.style.display =
                                        "none";

                                    document.body.appendChild(
                                        entry.canvas
                                    );
                                }


                                if (
                                    entry.canvas.width !==
                                        width ||
                                    entry.canvas.height !==
                                        height
                                )
                                {
                                    entry.canvas.width =
                                        width;

                                    entry.canvas.height =
                                        height;

                                    entry.context =
                                        entry.canvas.getContext(
                                            "2d",
                                            {
                                                willReadFrequently:
                                                    true
                                            }
                                        );
                                }


                                if (
                                    !entry.context
                                )
                                {
                                    try
                                    {
                                        videoFrame.close();
                                    }
                                    catch (e)
                                    {
                                    }

                                    return;
                                }


                                /*
                                 * Render decoded VideoFrame.
                                 */
                                entry.context.drawImage(
                                    videoFrame,
                                    0,
                                    0,
                                    width,
                                    height
                                );


                                /*
                                 * Read RGBA pixels.
                                 */
                                entry.lastImageData =
                                    entry.context.getImageData(
                                        0,
                                        0,
                                        width,
                                        height
                                    );


                                entry.width =
                                    width;

                                entry.height =
                                    height;

                                entry.ready =
                                    true;


                                /*
                                 * Release browser video memory.
                                 */
                                try
                                {
                                    videoFrame.close();
                                }
                                catch (e)
                                {
                                }
                            },

                        error:
                            function(error)
                            {
                                if (
                                    entry.generation !==
                                        generation
                                )
                                {
                                    return;
                                }

                                console.error(
                                    "IBVAP mobile WebCodecs decoder error:",
                                    error
                                );

                                entry.error =
                                    true;

                                entry.ready =
                                    false;
                            }
                    }
                );


                /*
                 * The mobile encoder uses H.264 baseline
                 * profile, level 3.1.
                 */
                decoder.configure(
                {
                    codec:
                        "avc1.42E01F",

                    optimizeForLatency:
                        true
                });


                entry.decoder =
                    decoder;
            }
            catch (error)
            {
                console.error(
                    "IBVAP mobile: failed to create/configure WebCodecs decoder:",
                    error
                );

                entry.error =
                    true;

                entry.started =
                    false;

                return -1;
            }


            /*
             * ------------------------------------------------
             * Viewer WebSocket.
             * ------------------------------------------------
             */

            let socket =
                null;

            try
            {
                socket =
                    new WebSocket(
                        websocketUrl
                    );
            }
            catch (error)
            {
                console.error(
                    "IBVAP mobile: WebSocket creation failed:",
                    error
                );

                try
                {
                    decoder.close();
                }
                catch (e)
                {
                }

                entry.decoder =
                    null;

                entry.error =
                    true;

                entry.started =
                    false;

                return -1;
            }


            socket.binaryType =
                "arraybuffer";

            entry.socket =
                socket;


            /*
             * ------------------------------------------------
             * WebSocket opened.
             * ------------------------------------------------
             */

            socket.onopen =
                function()
                {
                    if (
                        entry.generation !==
                            generation ||
                        !entry.mobile ||
                        !entry.started
                    )
                    {
                        try
                        {
                            socket.close();
                        }
                        catch (e)
                        {
                        }

                        return;
                    }


                    try
                    {
                        socket.send(
                            "VIEWER:" +
                            id
                        );
                    }
                    catch (error)
                    {
                        console.error(
                            "IBVAP mobile: viewer registration failed:",
                            error
                        );

                        entry.error =
                            true;

                        entry.ready =
                            false;
                    }
                };


            /*
             * ------------------------------------------------
             * Binary H.264 receiver.
             * ------------------------------------------------
             */

            socket.onmessage =
                function(event)
                {
                    if (
                        entry.generation !==
                            generation ||
                        !entry.mobile ||
                        !entry.started
                    )
                    {
                        return;
                    }


                    /*
                     * Control/text messages are not video.
                     */
                    if (
                        typeof event.data ===
                            "string"
                    )
                    {
                        return;
                    }


                    if (
                        !entry.decoder
                    )
                    {
                        return;
                    }


                    /*
                     * The server uses ArrayBuffer.
                     */
                    if (
                        !(event.data instanceof
                            ArrayBuffer)
                    )
                    {
                        return;
                    }


                    const buffer =
                        event.data;


                    if (
                        buffer.byteLength === 0
                    )
                    {
                        return;
                    }


                    /*
                     * ------------------------------------------------
                     * Backpressure.
                     * ------------------------------------------------
                     *
                     * The phone is configured for low latency.
                     * Do not allow the browser decoder queue to
                     * grow indefinitely.
                     *
                     * If the queue is temporarily full, discard
                     * the delta frame. The next IDR frame will
                     * recover the decoder.
                     * ------------------------------------------------
                     */

                    if (
                        entry.decoder.decodeQueueSize >=
                            3
                    )
                    {
                        /*
                         * Keep keyframes whenever possible.
                         * Dropping a delta frame is safe.
                         */
                        if (
                            !containsIdr(
                                buffer
                            )
                        )
                        {
                            return;
                        }
                    }


                    /*
                     * ------------------------------------------------
                     * Determine H.264 frame type.
                     * ------------------------------------------------
                     *
                     * IDR access unit:
                     *     WebCodecs "key"
                     *
                     * Everything else:
                     *     WebCodecs "delta"
                     * ------------------------------------------------
                     */

                    const isKey =
                        containsIdr(
                            buffer
                        );


                    /*
                     * WebCodecs timestamps must be monotonically
                     * increasing.
                     *
                     * The actual phone PTS is not transmitted by
                     * the current binary protocol, so use a local
                     * monotonically increasing timeline.
                     */
                    const timestamp =
                        Math.max(
                            Math.round(
                                performance.now() *
                                1000
                            ),
                            entry.decodeTimestamp + 1
                        );

                    entry.decodeTimestamp =
                        timestamp;


                    try
                    {
                        const chunk =
                            new EncodedVideoChunk(
                            {
                                type:
                                    isKey
                                        ? "key"
                                        : "delta",

                                timestamp:
                                    timestamp,

                                data:
                                    new Uint8Array(
                                        buffer
                                    )
                            }
                        );


                        entry.decoder.decode(
                            chunk
                        );
                    }
                    catch (error)
                    {
                        console.error(
                            "IBVAP mobile: WebCodecs decode error:",
                            error
                        );

                        /*
                         * Do not immediately destroy the
                         * connection. The decoder may recover
                         * at the next IDR.
                         */
                    }
                };


            /*
             * ------------------------------------------------
             * WebSocket error.
             * ------------------------------------------------
             */

            socket.onerror =
                function(error)
                {
                    if (
                        entry.generation !==
                            generation
                    )
                    {
                        return;
                    }

                    console.error(
                        "IBVAP mobile WebSocket error:",
                        error
                    );
                };


            /*
             * ------------------------------------------------
             * WebSocket closed.
             * ------------------------------------------------
             */

            socket.onclose =
                function(event)
                {
                    if (
                        entry.generation !==
                            generation
                    )
                    {
                        return;
                    }


                    entry.socket =
                        null;


                    if (
                        entry.decoder
                    )
                    {
                        try
                        {
                            entry.decoder.close();
                        }
                        catch (e)
                        {
                        }

                        entry.decoder =
                            null;
                    }


                    entry.ready =
                        false;


                    if (
                        entry.started
                    )
                    {
                        console.warn(
                            "IBVAP mobile viewer WebSocket closed:",
                            event.code,
                            event.reason
                        );

                        entry.error =
                            true;
                    }
                };


            return 1;
        }


        /*
         * ----------------------------------------------------
         * Unsupported browser source
         * ----------------------------------------------------
         */

        console.warn(
            "IBVAP browser source type not implemented:",
            sourceType
        );

        entry.error =
            true;

        return -1;
    }
);


/*
 * ============================================================
 * Source state
 * ============================================================
 */

EM_JS(
    int,
    ibvap_web_source_state,
    (
        const char* sourceId
    ),
    {
        if (
            !Module.IBVAPVideoSources
        )
        {
            return 0;
        }

        const id =
            UTF8ToString(sourceId);

        const entry =
            Module.IBVAPVideoSources[id];

        if (!entry)
        {
            return 0;
        }

        if (
            entry.error
        )
        {
            return -1;
        }

        if (
            !entry.started
        )
        {
            return 0;
        }


        /*
         * Mobile Stream does not have an HTML video element.
         */
        if (
            entry.mobile
        )
        {
            if (
                entry.ready &&
                entry.width > 0 &&
                entry.height > 0 &&
                entry.lastImageData
            )
            {
                return 2;
            }

            return 1;
        }


        /*
         * HTML video sources.
         */
        if (
            entry.video &&
            entry.video.readyState >= 2 &&
            entry.video.videoWidth > 0 &&
            entry.video.videoHeight > 0
        )
        {
            entry.width =
                entry.video.videoWidth;

            entry.height =
                entry.video.videoHeight;

            entry.ready =
                true;
        }

        if (
            entry.ready
        )
        {
            return 2;
        }

        return 1;
    }
);


/*
 * ============================================================
 * Get frame
 * ============================================================
 */

EM_JS(
    int,
    ibvap_web_get_frame_info,
    (
        const char* sourceId,
        int* widthOut,
        int* heightOut
    ),
    {
        if (
            !Module.IBVAPVideoSources
        )
        {
            return 0;
        }

        const id =
            UTF8ToString(sourceId);

        const entry =
            Module.IBVAPVideoSources[id];

        if (!entry)
        {
            return 0;
        }


        /*
         * ----------------------------------------------------
         * Mobile Stream
         * ----------------------------------------------------
         *
         * WebCodecs already rendered the latest frame into
         * the canvas.
         * ----------------------------------------------------
         */

        if (
            entry.mobile
        )
        {
            if (
                !entry.canvas ||
                !entry.context ||
                !entry.lastImageData ||
                entry.width <= 0 ||
                entry.height <= 0
            )
            {
                return 0;
            }

            HEAP32[
                widthOut >> 2
            ] =
                entry.width;

            HEAP32[
                heightOut >> 2
            ] =
                entry.height;

            return 1;
        }


        /*
         * ----------------------------------------------------
         * HTML video sources
         * ----------------------------------------------------
         */

        if (
            !entry.video
        )
        {
            return 0;
        }

        const video =
            entry.video;

        const width =
            video.videoWidth;

        const height =
            video.videoHeight;

        if (
            width <= 0 ||
            height <= 0
        )
        {
            return 0;
        }

        entry.width =
            width;

        entry.height =
            height;


        /*
         * Create canvas.
         */
        if (
            !entry.canvas ||
            entry.canvas.width !== width ||
            entry.canvas.height !== height
        )
        {
            if (
                !entry.canvas
            )
            {
                entry.canvas =
                    document.createElement(
                        "canvas"
                    );

                entry.canvas.style.display =
                    "none";

                document.body.appendChild(
                    entry.canvas
                );
            }

            entry.canvas.width =
                width;

            entry.canvas.height =
                height;

            entry.context =
                entry.canvas.getContext(
                    "2d",
                    {
                        willReadFrequently:
                            true
                    }
                );
        }

        if (
            !entry.context
        )
        {
            return 0;
        }


        /*
         * Camera:
         * mirror horizontally.
         */
        if (
            entry.type === 1
        )
        {
            entry.context.save();

            entry.context.translate(
                width,
                0
            );

            entry.context.scale(
                -1,
                1
            );

            entry.context.drawImage(
                video,
                0,
                0,
                width,
                height
            );

            entry.context.restore();
        }
        else
        {
            /*
             * Local video:
             * normal orientation.
             */
            entry.context.drawImage(
                video,
                0,
                0,
                width,
                height
            );
        }


        /*
         * Read RGBA pixels.
         */
        entry.lastImageData =
            entry.context.getImageData(
                0,
                0,
                width,
                height
            );


        HEAP32[
            widthOut >> 2
        ] =
            width;

        HEAP32[
            heightOut >> 2
        ] =
            height;

        return 1;
    }
);


/*
 * ============================================================
 * Copy RGBA frame into WASM memory
 * ============================================================
 */

EM_JS(
    int,
    ibvap_web_copy_frame,
    (
        const char* sourceId,
        unsigned char* destination,
        int destinationSize
    ),
    {
        if (
            !Module.IBVAPVideoSources
        )
        {
            return 0;
        }

        const id =
            UTF8ToString(sourceId);

        const entry =
            Module.IBVAPVideoSources[id];

        if (
            !entry ||
            !entry.lastImageData
        )
        {
            return 0;
        }

        const source =
            entry.lastImageData.data;

        const size =
            source.length;

        if (
            destinationSize < size
        )
        {
            return -1;
        }

        HEAPU8.set(
            source,
            destination
        );

        return size;
    }
);


/*
 * ============================================================
 * Stop source
 * ============================================================
 */

EM_JS(
    void,
    ibvap_web_stop_source,
    (
        const char* sourceId
    ),
    {
        if (
            !Module.IBVAPVideoSources
        )
        {
            return;
        }

        const id =
            UTF8ToString(sourceId);

        const entry =
            Module.IBVAPVideoSources[id];

        if (!entry)
        {
            return;
        }


        /*
         * Invalidate asynchronous callbacks first.
         */
        entry.generation =
            (entry.generation || 0) + 1;


        /*
         * Stop mobile WebSocket.
         */
        if (
            entry.socket
        )
        {
            try
            {
                entry.socket.onopen =
                    null;

                entry.socket.onmessage =
                    null;

                entry.socket.onerror =
                    null;

                entry.socket.onclose =
                    null;

                entry.socket.close();
            }
            catch (e)
            {
            }

            entry.socket =
                null;
        }


        /*
         * Stop WebCodecs decoder.
         */
        if (
            entry.decoder
        )
        {
            try
            {
                entry.decoder.close();
            }
            catch (e)
            {
            }

            entry.decoder =
                null;
        }


        /*
         * Stop camera tracks.
         */
        if (
            entry.stream
        )
        {
            const tracks =
                entry.stream.getTracks();

            for (
                let i = 0;
                i < tracks.length;
                ++i
            )
            {
                tracks[i].stop();
            }

            entry.stream =
                null;
        }


        /*
         * Remove video element.
         */
        if (
            entry.video
        )
        {
            try
            {
                entry.video.pause();
            }
            catch (e)
            {
            }

            entry.video.srcObject =
                null;

            entry.video.removeAttribute(
                "src"
            );

            entry.video.load();

            if (
                entry.video.parentNode
            )
            {
                entry.video.parentNode
                    .removeChild(
                        entry.video
                    );
            }

            entry.video =
                null;
        }


        /*
         * Remove canvas.
         */
        if (
            entry.canvas
        )
        {
            if (
                entry.canvas.parentNode
            )
            {
                entry.canvas.parentNode
                    .removeChild(
                        entry.canvas
                    );
            }

            entry.canvas =
                null;

            entry.context =
                null;
        }


        entry.started =
            false;

        entry.ready =
            false;

        entry.error =
            false;

        entry.mobile =
            false;

        entry.width =
            0;

        entry.height =
            0;

        entry.lastImageData =
            null;

        entry.decodeTimestamp =
            0;
    }
);


/*
 * ============================================================
 * Destroy source
 * ============================================================
 */

EM_JS(
    void,
    ibvap_web_destroy_source,
    (
        const char* sourceId
    ),
    {
        if (
            !Module.IBVAPVideoSources
        )
        {
            return;
        }

        const id =
            UTF8ToString(sourceId);

        const entry =
            Module.IBVAPVideoSources[id];

        if (!entry)
        {
            return;
        }


        /*
         * Invalidate asynchronous callbacks.
         */
        entry.generation =
            (entry.generation || 0) + 1;


        /*
         * Stop mobile WebSocket.
         */
        if (
            entry.socket
        )
        {
            try
            {
                entry.socket.onopen =
                    null;

                entry.socket.onmessage =
                    null;

                entry.socket.onerror =
                    null;

                entry.socket.onclose =
                    null;

                entry.socket.close();
            }
            catch (e)
            {
            }

            entry.socket =
                null;
        }


        /*
         * Stop WebCodecs decoder.
         */
        if (
            entry.decoder
        )
        {
            try
            {
                entry.decoder.close();
            }
            catch (e)
            {
            }

            entry.decoder =
                null;
        }


        /*
         * Stop camera tracks.
         */
        if (
            entry.stream
        )
        {
            const tracks =
                entry.stream.getTracks();

            for (
                let i = 0;
                i < tracks.length;
                ++i
            )
            {
                tracks[i].stop();
            }
        }


        /*
         * Remove video.
         */
        if (
            entry.video
        )
        {
            try
            {
                entry.video.pause();
            }
            catch (e)
            {
            }

            entry.video.srcObject =
                null;

            entry.video.removeAttribute(
                "src"
            );

            entry.video.load();

            if (
                entry.video.parentNode
            )
            {
                entry.video.parentNode
                    .removeChild(
                        entry.video
                    );
            }
        }


        /*
         * Remove canvas.
         */
        if (
            entry.canvas
        )
        {
            if (
                entry.canvas.parentNode
            )
            {
                entry.canvas.parentNode
                    .removeChild(
                        entry.canvas
                    );
            }
        }


        delete
            Module.IBVAPVideoSources[id];
    }
);


#endif


/*
 * ============================================================
 * Constructor
 * ============================================================
 */

WebVideoSource::WebVideoSource(
    VideoSourceType type,
    std::string sourceId,
    std::string displayName,
    std::string sourceUrl
)
    : m_sourceUrl(
        std::move(
            sourceUrl
        )
    )
{
    m_info.id =
        std::move(
            sourceId
        );

    m_info.name =
        std::move(
            displayName
        );

    m_info.type =
        type;

    m_info.width =
        0;

    m_info.height =
        0;

    m_info.frameRate =
        0.0;

    m_info.address =
        m_sourceUrl;

    m_info.deviceConnected =
        false;
}


/*
 * ============================================================
 * Destructor
 * ============================================================
 */

WebVideoSource::~WebVideoSource()
{
    stop();

#ifdef __EMSCRIPTEN__

    ibvap_web_destroy_source(
        m_info.id.c_str()
    );

#endif
}


/*
 * ============================================================
 * Start
 * ============================================================
 */

bool WebVideoSource::start()
{
#ifdef __EMSCRIPTEN__

    int browserType =
        0;

    switch (
        m_info.type
    )
    {
        case VideoSourceType::LaptopCamera:
            browserType = 1;
            break;

        case VideoSourceType::LocalVideo:
            browserType = 2;
            break;

        case VideoSourceType::MobileStream:
            browserType = 3;
            break;

        case VideoSourceType::RTSP:
            browserType = 4;
            break;

        default:

            m_state =
                VideoSourceState::Error;

            return false;
    }


    const int result =
        ibvap_web_start_source(
            m_info.id.c_str(),
            browserType,
            m_sourceUrl.c_str()
        );


    if (
        result < 0
    )
    {
        m_state =
            VideoSourceState::Error;

        m_info.deviceConnected =
            false;

        return false;
    }


    m_state =
        VideoSourceState::WaitingForDevice;

    m_info.deviceConnected =
        false;

    return true;

#else

    m_state =
        VideoSourceState::Error;

    return false;

#endif
}


/*
 * ============================================================
 * Stop
 * ============================================================
 */

void WebVideoSource::stop() noexcept
{
#ifdef __EMSCRIPTEN__

    ibvap_web_stop_source(
        m_info.id.c_str()
    );

#endif

    m_state =
        VideoSourceState::Stopped;

    m_info.deviceConnected =
        false;

    m_info.width =
        0;

    m_info.height =
        0;

    m_info.frameRate =
        0.0;

    m_sequence =
        0;

    m_frameBuffer.reset();
}


/*
 * ============================================================
 * State
 * ============================================================
 */

VideoSourceState
WebVideoSource::state()
    const noexcept
{
#ifdef __EMSCRIPTEN__

    const int browserState =
        ibvap_web_source_state(
            m_info.id.c_str()
        );


    if (
        browserState < 0
    )
    {
        m_state =
            VideoSourceState::Error;

        m_info.deviceConnected =
            false;

        return m_state;
    }


    if (
        browserState == 0
    )
    {
        if (
            m_state !=
            VideoSourceState::Stopped
        )
        {
            m_state =
                VideoSourceState::Created;
        }

        m_info.deviceConnected =
            false;

        return m_state;
    }


    if (
        browserState == 1
    )
    {
        m_state =
            VideoSourceState::WaitingForDevice;

        m_info.deviceConnected =
            false;

        return m_state;
    }


    m_state =
        VideoSourceState::Running;

    m_info.deviceConnected =
        true;

    return m_state;

#else

    return m_state;

#endif
}


/*
 * ============================================================
 * Try get frame
 * ============================================================
 */

bool WebVideoSource::tryGetFrame(
    Frame& frame
)
{
    frame =
        Frame{};

#ifdef __EMSCRIPTEN__

    if (
        state() !=
        VideoSourceState::Running
    )
    {
        return false;
    }


    int width =
        0;

    int height =
        0;


    if (
        ibvap_web_get_frame_info(
            m_info.id.c_str(),
            &width,
            &height
        ) == 0
    )
    {
        return false;
    }


    if (
        width <= 0 ||
        height <= 0
    )
    {
        return false;
    }


    const std::size_t
        stride =
            static_cast<
                std::size_t
            >(width) * 4;


    const std::size_t
        requiredSize =
            stride *
            static_cast<
                std::size_t
            >(height);


    /*
     * Allocate frame buffer.
     */
    if (
        !m_frameBuffer ||
        m_frameBuffer->data.size() !=
            requiredSize
    )
    {
        m_frameBuffer =
            std::make_shared<
                FrameBuffer
            >();

        m_frameBuffer->data.resize(
            requiredSize
        );
    }


    /*
     * Copy pixels.
     */
    const int copied =
        ibvap_web_copy_frame(
            m_info.id.c_str(),
            m_frameBuffer->data.data(),
            static_cast<
                int
            >(requiredSize)
        );


    if (
        copied <= 0
    )
    {
        return false;
    }


    /*
     * Update source information.
     */
    m_info.width =
        static_cast<
            std::uint32_t
        >(width);

    m_info.height =
        static_cast<
            std::uint32_t
        >(height);

    m_info.frameRate =
        30.0;


    /*
     * Fill frame.
     */
    frame.buffer =
        m_frameBuffer;

    frame.width =
        static_cast<
            std::uint32_t
        >(width);

    frame.height =
        static_cast<
            std::uint32_t
        >(height);

    frame.stride =
        static_cast<
            std::uint32_t
        >(stride);

    frame.format =
        PixelFormat::RGBA8;

    frame.timestampUs =
        static_cast<
            std::int64_t
        >(
            emscripten_get_now()
            * 1000.0
        );

    frame.sequence =
        ++m_sequence;


    return frame.valid();

#else

    return false;

#endif
}


/*
 * ============================================================
 * Source information
 * ============================================================
 */

const VideoSourceInfo&
WebVideoSource::info()
    const noexcept
{
    return m_info;
}