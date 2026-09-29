#include "MobileStreamSource.hpp"

#include "MobileStreamServer.hpp"

#include <utility>

MobileStreamSource::MobileStreamSource(
    MobileStreamServer& server,
    const std::string& streamId,
    const std::string& displayName
)
    : m_server(server),
      m_streamId(streamId)
{
    m_info.id =
        streamId;

    m_info.name =
        displayName;

    m_info.type =
        VideoSourceType::MobileStream;

    m_info.deviceConnected =
        false;

    m_state =
        VideoSourceState::WaitingForDevice;
}

MobileStreamSource::~MobileStreamSource()
{
    stop();
}

bool MobileStreamSource::start()
{
    if (
        m_state ==
        VideoSourceState::Running
    )
    {
        return true;
    }

    if (
        !m_server.isRunning()
    )
    {
        m_state =
            VideoSourceState::Error;

        return false;
    }

    m_state =
        VideoSourceState::WaitingForDevice;

    return true;
}

void MobileStreamSource::stop() noexcept
{
    m_state =
        VideoSourceState::Stopped;

    m_info.deviceConnected =
        false;
}

VideoSourceState
MobileStreamSource::state()
    const noexcept
{
    if (
        m_state ==
        VideoSourceState::Stopped
    )
    {
        return VideoSourceState::Stopped;
    }

    if (
        m_server.isConnected(
            m_streamId
        )
    )
    {
        return VideoSourceState::Running;
    }

    return VideoSourceState::WaitingForDevice;
}

bool MobileStreamSource::tryGetFrame(
    Frame& frame
)
{
    Frame latestFrame;

    if (
        !m_server.tryGetFrame(
            m_streamId,
            latestFrame
        )
    )
    {
        m_info.deviceConnected =
            m_server.isConnected(
                m_streamId
            );

        return false;
    }

    if (
        latestFrame.sequence ==
        m_lastSequence
    )
    {
        return false;
    }

    m_lastSequence =
        latestFrame.sequence;

    frame =
        std::move(
            latestFrame
        );

    m_info.width =
        frame.width;

    m_info.height =
        frame.height;

    m_info.deviceConnected =
        true;

    m_info.frameRate =
        0.0;

    m_state =
        VideoSourceState::Running;

    return true;
}

const VideoSourceInfo&
MobileStreamSource::info()
    const noexcept
{
    return m_info;
}