#include "NetworkService.hpp"

#include <algorithm>
#include <iostream>
#include <string>

#ifdef _WIN32

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <windows.h>

#else

    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>

#endif

namespace
{
    void addUniqueAddress(
        std::vector<std::string>& addresses,
        const std::string& address
    )
    {
        if (address.empty())
        {
            return;
        }

        if (
            std::find(
                addresses.begin(),
                addresses.end(),
                address
            ) == addresses.end()
        )
        {
            addresses.push_back(address);
        }
    }
}

NetworkService::NetworkService()
{
}

NetworkService::~NetworkService()
{
    stop();
}

bool NetworkService::start()
{
    if (m_running)
    {
        return true;
    }

#ifdef _WIN32

    WSADATA wsaData{};

    const int result =
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData
        );

    if (result != 0)
    {
        std::cerr
            << "NetworkService: "
               "WSAStartup failed. Error = "
            << result
            << '\n';

        return false;
    }

#endif

    if (!discoverLocalAddresses())
    {
#ifdef _WIN32
        WSACleanup();
#endif

        std::cerr
            << "NetworkService: "
               "Could not discover any usable "
               "local IPv4 address.\n";

        return false;
    }

    m_running = true;

    std::cout
        << "NetworkService initialized.\n";

    std::cout
        << "  Discovered local IPv4 addresses:\n";

    for (
        const std::string& address :
        m_localAddresses
    )
    {
        std::cout
            << "    "
            << address
            << '\n';
    }

    std::cout
        << "  Primary local address: "
        << m_localAddress
        << '\n';

    return true;
}

void NetworkService::stop() noexcept
{
    if (!m_running)
    {
        return;
    }

    m_running = false;

    m_port = 0;

    m_localAddress.clear();

    m_localAddresses.clear();

#ifdef _WIN32

    WSACleanup();

#endif

    std::cout
        << "NetworkService stopped.\n";
}

bool NetworkService::isRunning()
    const noexcept
{
    return m_running;
}

std::uint16_t
NetworkService::port()
    const noexcept
{
    return m_port;
}

const std::string&
NetworkService::localAddress()
    const noexcept
{
    return m_localAddress;
}

const std::vector<std::string>&
NetworkService::localAddresses()
    const noexcept
{
    return m_localAddresses;
}

bool NetworkService::isPrivateIPv4(
    std::uint32_t address
)
{
    const std::uint32_t hostAddress =
        ntohl(address);

    /*
     * 10.0.0.0/8
     */
    if (
        (hostAddress & 0xFF000000u) ==
        0x0A000000u
    )
    {
        return true;
    }

    /*
     * 172.16.0.0/12
     */
    if (
        (hostAddress & 0xFFF00000u) ==
        0xAC100000u
    )
    {
        return true;
    }

    /*
     * 192.168.0.0/16
     */
    if (
        (hostAddress & 0xFFFF0000u) ==
        0xC0A80000u
    )
    {
        return true;
    }

    return false;
}

bool NetworkService::isUsableIPv4(
    std::uint32_t address
)
{
    const std::uint32_t hostAddress =
        ntohl(address);

    /*
     * 0.0.0.0
     */
    if (hostAddress == 0)
    {
        return false;
    }

    /*
     * 127.0.0.0/8
     *
     * Loopback is not useful for another
     * physical device such as a phone.
     */
    if (
        (hostAddress & 0xFF000000u) ==
        0x7F000000u
    )
    {
        return false;
    }

    /*
     * 169.254.0.0/16
     *
     * Automatic Private IP Addressing.
     */
    if (
        (hostAddress & 0xFFFF0000u) ==
        0xA9FE0000u
    )
    {
        return false;
    }

    return true;
}

bool NetworkService::discoverLocalAddresses()
{
    m_localAddresses.clear();

    m_localAddress.clear();

#ifdef _WIN32

    /*
     * ---------------------------------------------------------
     * WINDOWS
     * ---------------------------------------------------------
     *
     * GetAdaptersAddresses gives us the actual network
     * interfaces instead of relying on gethostname().
     *
     * This allows IBVAP to see:
     *
     *   Wi-Fi
     *   Ethernet
     *   Mobile Hotspot
     *   Other usable adapters
     *
     * independently.
     */

    ULONG bufferSize = 15000;

    std::vector<unsigned char>
        buffer(
            bufferSize
        );

    IP_ADAPTER_ADDRESSES* adapters =
        reinterpret_cast<
            IP_ADAPTER_ADDRESSES*
        >(
            buffer.data()
        );

    ULONG result =
        GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,
            adapters,
            &bufferSize
        );

    if (
        result ==
        ERROR_BUFFER_OVERFLOW
    )
    {
        buffer.resize(
            bufferSize
        );

        adapters =
            reinterpret_cast<
                IP_ADAPTER_ADDRESSES*
            >(
                buffer.data()
            );

        result =
            GetAdaptersAddresses(
                AF_INET,
                GAA_FLAG_SKIP_ANYCAST |
                GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER,
                nullptr,
                adapters,
                &bufferSize
            );
    }

    if (result != NO_ERROR)
    {
        std::cerr
            << "NetworkService: "
               "GetAdaptersAddresses failed. "
               "Error = "
            << result
            << '\n';

        return false;
    }

    /*
     * First collect private IPv4 addresses.
     */
    for (
        IP_ADAPTER_ADDRESSES* adapter =
            adapters;
        adapter != nullptr;
        adapter =
            adapter->Next
    )
    {
        for (
            IP_ADAPTER_UNICAST_ADDRESS* unicast =
                adapter->FirstUnicastAddress;
            unicast != nullptr;
            unicast =
                unicast->Next
        )
        {
            if (
                unicast->Address.lpSockaddr ==
                nullptr
            )
            {
                continue;
            }

            if (
                unicast->Address.lpSockaddr->sa_family
                != AF_INET
            )
            {
                continue;
            }

            const auto* address =
                reinterpret_cast<
                    const sockaddr_in*
                >(
                    unicast->Address.lpSockaddr
                );

            const std::uint32_t ipv4 =
                address->sin_addr.s_addr;

            if (!isUsableIPv4(ipv4))
            {
                continue;
            }

            char addressBuffer[
                INET_ADDRSTRLEN
            ] = {};

            const char* converted =
                inet_ntop(
                    AF_INET,
                    &address->sin_addr,
                    addressBuffer,
                    sizeof(addressBuffer)
                );

            if (converted == nullptr)
            {
                continue;
            }

            const std::string addressString =
                converted;

            /*
             * Prefer private LAN addresses.
             */
            if (isPrivateIPv4(ipv4))
            {
                addUniqueAddress(
                    m_localAddresses,
                    addressString
                );
            }
        }
    }

#else

    /*
     * ---------------------------------------------------------
     * LINUX / macOS / UNIX-LIKE SYSTEMS
     * ---------------------------------------------------------
     *
     * getifaddrs is available on the major Unix-like
     * platforms and lets us enumerate network interfaces
     * without introducing platform-specific code into
     * the rest of IBVAP.
     */

    ifaddrs* interfaces = nullptr;

    if (
        getifaddrs(
            &interfaces
        ) != 0
    )
    {
        std::cerr
            << "NetworkService: "
               "getifaddrs failed.\n";

        return false;
    }

    for (
        ifaddrs* current =
            interfaces;
        current != nullptr;
        current =
            current->ifa_next
    )
    {
        if (
            current->ifa_addr ==
            nullptr
        )
        {
            continue;
        }

        if (
            current->ifa_addr->sa_family
            != AF_INET
        )
        {
            continue;
        }

        const auto* address =
            reinterpret_cast<
                const sockaddr_in*
            >(
                current->ifa_addr
            );

        const std::uint32_t ipv4 =
            address->sin_addr.s_addr;

        if (!isUsableIPv4(ipv4))
        {
            continue;
        }

        if (!isPrivateIPv4(ipv4))
        {
            continue;
        }

        char addressBuffer[
            INET_ADDRSTRLEN
        ] = {};

        const char* converted =
            inet_ntop(
                AF_INET,
                &address->sin_addr,
                addressBuffer,
                sizeof(addressBuffer)
            );

        if (converted == nullptr)
        {
            continue;
        }

        addUniqueAddress(
            m_localAddresses,
            converted
        );
    }

    freeifaddrs(
        interfaces
    );

#endif

    /*
     * We currently require at least one private
     * IPv4 address because the current local device
     * connection mechanism is intended for local
     * networks.
     */
    if (m_localAddresses.empty())
    {
        return false;
    }

    /*
     * Keep the first discovered private address as
     * the backwards-compatible primary address.
     *
     * The UI/network layer will later expose the
     * complete list and allow the appropriate
     * interface to be selected automatically.
     */
    m_localAddress =
        m_localAddresses.front();

    return true;
}