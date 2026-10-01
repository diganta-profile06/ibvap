#pragma once

#include <cstdint>
#include <string>
#include <vector>

class MobileNetwork
{
public:
    struct LocalAddress
    {
        std::string address;
        std::string interfaceName;
    };

    static std::vector<LocalAddress>
    getLocalIPv4Addresses();

    static std::string
    choosePrimaryAddress(
        const std::vector<LocalAddress>& addresses
    );

    static bool
    generateCertificate(
        const std::string& certificatePath,
        const std::string& privateKeyPath,
        const std::vector<std::string>& ipAddresses
    );

    static std::string
    buildMobileUrl(
        const std::string& ipAddress,
        std::uint16_t port,
        const std::string& sourceId
    );
};