#pragma once

#include <cstdint>
#include <string>
#include <vector>

class NetworkService
{
public:
    NetworkService();

    ~NetworkService();

    NetworkService(
        const NetworkService&
    ) = delete;

    NetworkService& operator=(
        const NetworkService&
    ) = delete;

    bool start();

    void stop() noexcept;

    bool isRunning() const noexcept;

    std::uint16_t port() const noexcept;

    const std::string& localAddress()
        const noexcept;

    const std::vector<std::string>&
    localAddresses() const noexcept;

private:
    bool discoverLocalAddresses();

    static bool isPrivateIPv4(
        std::uint32_t address
    );

    static bool isUsableIPv4(
        std::uint32_t address
    );

private:
    bool m_running = false;

    std::uint16_t m_port = 0;

    std::string m_localAddress;

    std::vector<std::string>
        m_localAddresses;
};