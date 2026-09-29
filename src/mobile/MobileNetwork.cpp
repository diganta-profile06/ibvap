#include "MobileNetwork.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#else

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#endif

namespace
{

/*
 * =============================================================
 * IPv4 CLASSIFICATION
 * =============================================================
 */

bool isPrivateIPv4(
    const std::string& address
)
{
    unsigned int a = 0;
    unsigned int b = 0;

    if (
        std::sscanf(
            address.c_str(),
            "%u.%u",
            &a,
            &b
        ) != 2
    )
    {
        return false;
    }

    if (a == 10)
    {
        return true;
    }

    if (
        a == 172 &&
        b >= 16 &&
        b <= 31
    )
    {
        return true;
    }

    if (
        a == 192 &&
        b == 168
    )
    {
        return true;
    }

    return false;
}


/*
 * =============================================================
 * ADDRESS / INTERFACE PREFERENCE
 * =============================================================
 *
 * Lower value = better.
 *
 * Physical Wi-Fi/LAN interfaces are preferred.
 *
 * Virtual interfaces such as:
 *
 *   WSL
 *   Hyper-V
 *   VMware
 *   VirtualBox
 *   Docker
 *   VPN
 *   TAP
 *
 * are deliberately pushed down the list.
 */

int interfacePreference(
    const std::string& interfaceName
)
{
    std::string name =
        interfaceName;

    std::transform(
        name.begin(),
        name.end(),
        name.begin(),
        [](
            unsigned char character
        )
        {
            return static_cast<char>(
                std::tolower(
                    character
                )
            );
        }
    );


    /*
     * ---------------------------------------------------------
     * Strong virtual / tunnel indicators.
     * ---------------------------------------------------------
     */

    const char* virtualNames[] =
    {
        "wsl",
        "hyper-v",
        "hyperv",
        "veth",
        "vmware",
        "virtualbox",
        "virtual box",
        "docker",
        "tap",
        "tun",
        "vpn",
        "wireguard",
        "tailscale",
        "zerotier",
        "hamachi",
        "loopback",
        "pseudo",
        "teredo"
    };


    for (
        const char* keyword :
        virtualNames
    )
    {
        if (
            name.find(
                keyword
            ) !=
            std::string::npos
        )
        {
            return 100;
        }
    }


    /*
     * ---------------------------------------------------------
     * Wi-Fi / WLAN.
     * ---------------------------------------------------------
     */

    const char* wirelessNames[] =
    {
        "wi-fi",
        "wifi",
        "wlan",
        "wireless",
        "802.11"
    };


    for (
        const char* keyword :
        wirelessNames
    )
    {
        if (
            name.find(
                keyword
            ) !=
            std::string::npos
        )
        {
            return 10;
        }
    }


    /*
     * ---------------------------------------------------------
     * Physical Ethernet.
     * ---------------------------------------------------------
     */

    const char* ethernetNames[] =
    {
        "ethernet",
        "lan"
    };


    for (
        const char* keyword :
        ethernetNames
    )
    {
        if (
            name.find(
                keyword
            ) !=
            std::string::npos
        )
        {
            return 20;
        }
    }


    /*
     * Unknown interfaces are still allowed.
     *
     * This keeps the implementation cross-platform and
     * prevents legitimate networks from being rejected merely
     * because their interface has an unexpected name.
     */

    return 50;
}


/*
 * =============================================================
 * IP ADDRESS PREFERENCE
 * =============================================================
 */

int addressPreference(
    const MobileNetwork::LocalAddress& address
)
{
    /*
     * Windows Mobile Hotspot commonly uses this subnet.
     *
     * Keep it highly preferred because it is specifically
     * useful when the laptop itself is providing the local
     * network for phones.
     */
    if (
        address.address.rfind(
            "192.168.137.",
            0
        ) == 0
    )
    {
        return 5;
    }


    const int interfaceRank =
        interfacePreference(
            address.interfaceName
        );


    /*
     * Keep interface preference dominant.
     */
    return interfaceRank;
}


/*
 * =============================================================
 * SORT COMPARATOR
 * =============================================================
 */

bool preferredAddressFirst(
    const MobileNetwork::LocalAddress& a,
    const MobileNetwork::LocalAddress& b
)
{
    const int aPreference =
        addressPreference(
            a
        );

    const int bPreference =
        addressPreference(
            b
        );


    if (
        aPreference !=
        bPreference
    )
    {
        return
            aPreference <
            bPreference;
    }


    return
        a.address <
        b.address;
}

}


/*
 * =============================================================
 * LOCAL IPv4 ADDRESSES
 * =============================================================
 */

std::vector<MobileNetwork::LocalAddress>
MobileNetwork::getLocalIPv4Addresses()
{
    std::vector<LocalAddress> result;


#ifdef _WIN32

    ULONG bufferSize = 0;

    DWORD status =
        GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,
            nullptr,
            &bufferSize
        );


    if (
        status !=
        ERROR_BUFFER_OVERFLOW
    )
    {
        return result;
    }


    std::vector<unsigned char> buffer(
        bufferSize
    );


    auto* adapters =
        reinterpret_cast<
            IP_ADAPTER_ADDRESSES*
        >(
            buffer.data()
        );


    status =
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
        status !=
        NO_ERROR
    )
    {
        return result;
    }


    for (
        auto* adapter = adapters;
        adapter != nullptr;
        adapter = adapter->Next
    )
    {
        if (
            adapter->OperStatus !=
            IfOperStatusUp
        )
        {
            continue;
        }


        /*
         * Explicitly ignore loopback and tunnel adapter
         * types at the Windows networking level.
         */
        if (
            adapter->IfType ==
            IF_TYPE_SOFTWARE_LOOPBACK
        )
        {
            continue;
        }


        if (
            adapter->IfType ==
            IF_TYPE_TUNNEL
        )
        {
            continue;
        }


        std::string interfaceName;


        if (
            adapter->FriendlyName !=
            nullptr
        )
        {
            char nameBuffer[
                512
            ] = {};


            WideCharToMultiByte(
                CP_UTF8,
                0,
                adapter->FriendlyName,
                -1,
                nameBuffer,
                sizeof(nameBuffer),
                nullptr,
                nullptr
            );


            interfaceName =
                nameBuffer;
        }


        for (
            auto* address =
                adapter->FirstUnicastAddress;
            address != nullptr;
            address =
                address->Next
        )
        {
            if (
                address->Address.lpSockaddr ==
                nullptr
            )
            {
                continue;
            }


            if (
                address->Address.lpSockaddr->sa_family !=
                AF_INET
            )
            {
                continue;
            }


            const auto* ipv4 =
                reinterpret_cast<
                    const sockaddr_in*
                >(
                    address->Address.lpSockaddr
                );


            char text[
                INET_ADDRSTRLEN
            ] = {};


            if (
                inet_ntop(
                    AF_INET,
                    &ipv4->sin_addr,
                    text,
                    sizeof(text)
                ) == nullptr
            )
            {
                continue;
            }


            std::string ip(
                text
            );


            if (
                !isPrivateIPv4(
                    ip
                )
            )
            {
                continue;
            }


            LocalAddress local;


            local.address =
                std::move(
                    ip
                );


            local.interfaceName =
                interfaceName;


            result.push_back(
                std::move(
                    local
                )
            );
        }
    }


#else

    struct ifaddrs* interfaces =
        nullptr;


    if (
        getifaddrs(
            &interfaces
        ) != 0
    )
    {
        return result;
    }


    for (
        auto* interfaceEntry =
            interfaces;
        interfaceEntry != nullptr;
        interfaceEntry =
            interfaceEntry->ifa_next
    )
    {
        if (
            interfaceEntry->ifa_addr ==
            nullptr
        )
        {
            continue;
        }


        if (
            interfaceEntry->ifa_addr->sa_family !=
            AF_INET
        )
        {
            continue;
        }


        /*
         * Ignore interfaces that are not administratively
         * up when the platform exposes the flags.
         */
        if (
            (
                interfaceEntry->ifa_flags &
                IFF_UP
            ) == 0
        )
        {
            continue;
        }


        const auto* ipv4 =
            reinterpret_cast<
                const sockaddr_in*
            >(
                interfaceEntry->ifa_addr
            );


        char text[
            INET_ADDRSTRLEN
        ] = {};


        if (
            inet_ntop(
                AF_INET,
                &ipv4->sin_addr,
                text,
                sizeof(text)
            ) == nullptr
        )
        {
            continue;
        }


        std::string ip(
            text
        );


        if (
            !isPrivateIPv4(
                ip
            )
        )
        {
            continue;
        }


        /*
         * Skip the obvious loopback address.
         */
        if (
            ip ==
            "127.0.0.1"
        )
        {
            continue;
        }


        LocalAddress local;


        local.address =
            std::move(
                ip
            );


        if (
            interfaceEntry->ifa_name !=
            nullptr
        )
        {
            local.interfaceName =
                interfaceEntry->ifa_name;
        }


        result.push_back(
            std::move(
                local
            )
        );
    }


    freeifaddrs(
        interfaces
    );

#endif


    /*
     * =========================================================
     * REMOVE DUPLICATES
     * =========================================================
     */

    std::sort(
        result.begin(),
        result.end(),
        preferredAddressFirst
    );


    result.erase(
        std::unique(
            result.begin(),
            result.end(),
            [](
                const LocalAddress& a,
                const LocalAddress& b
            )
            {
                return
                    a.address ==
                    b.address;
            }
        ),
        result.end()
    );


    return result;
}


/*
 * =============================================================
 * PRIMARY ADDRESS
 * =============================================================
 */

std::string
MobileNetwork::choosePrimaryAddress(
    const std::vector<LocalAddress>& addresses
)
{
    if (
        addresses.empty()
    )
    {
        return {};
    }


    /*
     * getLocalIPv4Addresses() already sorts addresses by
     * interface suitability.
     *
     * Re-evaluate here as well because this function may be
     * called with a vector created elsewhere.
     */

    const auto iterator =
        std::min_element(
            addresses.begin(),
            addresses.end(),
            preferredAddressFirst
        );


    if (
        iterator ==
        addresses.end()
    )
    {
        return {};
    }


    return iterator->address;
}


/*
 * =============================================================
 * CERTIFICATE GENERATION
 * =============================================================
 */

bool
MobileNetwork::generateCertificate(
    const std::string& certificatePath,
    const std::string& privateKeyPath,
    const std::vector<std::string>& ipAddresses
)
{
    if (
        ipAddresses.empty()
    )
    {
        return false;
    }


    std::filesystem::path certPath(
        certificatePath
    );


    std::filesystem::path keyPath(
        privateKeyPath
    );


    std::error_code error;


    std::filesystem::create_directories(
        certPath.parent_path(),
        error
    );


    std::filesystem::create_directories(
        keyPath.parent_path(),
        error
    );


    if (
        std::filesystem::exists(
            certPath
        ) &&
        std::filesystem::exists(
            keyPath
        )
    )
    {
        return true;
    }


    EVP_PKEY* key =
        EVP_PKEY_new();


    if (
        key == nullptr
    )
    {
        return false;
    }


    RSA* rsa =
        RSA_new();


    BIGNUM* exponent =
        BN_new();


    if (
        rsa == nullptr ||
        exponent == nullptr
    )
    {
        RSA_free(
            rsa
        );


        BN_free(
            exponent
        );


        EVP_PKEY_free(
            key
        );


        return false;
    }


    if (
        !BN_set_word(
            exponent,
            RSA_F4
        ) ||
        !RSA_generate_key_ex(
            rsa,
            2048,
            exponent,
            nullptr
        ) ||
        !EVP_PKEY_assign_RSA(
            key,
            rsa
        )
    )
    {
        RSA_free(
            rsa
        );


        BN_free(
            exponent
        );


        EVP_PKEY_free(
            key
        );


        return false;
    }


    /*
     * Ownership of RSA has moved into EVP_PKEY.
     */
    rsa =
        nullptr;


    BN_free(
        exponent
    );


    X509* certificate =
        X509_new();


    if (
        certificate == nullptr
    )
    {
        EVP_PKEY_free(
            key
        );


        return false;
    }


    X509_set_version(
        certificate,
        2
    );


    ASN1_INTEGER_set(
        X509_get_serialNumber(
            certificate
        ),
        1
    );


    X509_gmtime_adj(
        X509_get_notBefore(
            certificate
        ),
        0
    );


    X509_gmtime_adj(
        X509_get_notAfter(
            certificate
        ),
        60L * 60L * 24L * 365L
    );


    X509_set_pubkey(
        certificate,
        key
    );


    auto* subjectName =
        X509_get_subject_name(
            certificate
        );


    X509_NAME_add_entry_by_txt(
        subjectName,
        "CN",
        MBSTRING_ASC,
        reinterpret_cast<
            const unsigned char*
        >(
            "IBVAP Mobile"
        ),
        -1,
        -1,
        0
    );


    X509_set_issuer_name(
        certificate,
        subjectName
    );


    /*
     * =========================================================
     * SUBJECT ALTERNATIVE NAME
     * =========================================================
     */

    std::string san;


    for (
        std::size_t index = 0;
        index < ipAddresses.size();
        ++index
    )
    {
        if (
            index != 0
        )
        {
            san += ",";
        }


        san +=
            "IP:";


        san +=
            ipAddresses[index];
    }


    X509V3_CTX context;


    X509V3_set_ctx_nodb(
        &context
    );


    X509V3_set_ctx(
        &context,
        certificate,
        certificate,
        nullptr,
        nullptr,
        0
    );


    X509_EXTENSION* extension =
        X509V3_EXT_conf_nid(
            nullptr,
            &context,
            NID_subject_alt_name,
            san.data()
        );


    if (
        extension == nullptr
    )
    {
        X509_free(
            certificate
        );


        EVP_PKEY_free(
            key
        );


        return false;
    }


    X509_add_ext(
        certificate,
        extension,
        -1
    );


    X509_EXTENSION_free(
        extension
    );


    if (
        !X509_sign(
            certificate,
            key,
            EVP_sha256()
        )
    )
    {
        X509_free(
            certificate
        );


        EVP_PKEY_free(
            key
        );


        return false;
    }


    FILE* keyFile =
        nullptr;


    FILE* certFile =
        nullptr;


#ifdef _WIN32

    fopen_s(
        &keyFile,
        privateKeyPath.c_str(),
        "wb"
    );


    fopen_s(
        &certFile,
        certificatePath.c_str(),
        "wb"
    );

#else

    keyFile =
        std::fopen(
            privateKeyPath.c_str(),
            "wb"
        );


    certFile =
        std::fopen(
            certificatePath.c_str(),
            "wb"
        );

#endif


    if (
        keyFile == nullptr ||
        certFile == nullptr
    )
    {
        if (
            keyFile != nullptr
        )
        {
            std::fclose(
                keyFile
            );
        }


        if (
            certFile != nullptr
        )
        {
            std::fclose(
                certFile
            );
        }


        X509_free(
            certificate
        );


        EVP_PKEY_free(
            key
        );


        return false;
    }


    const bool keyWritten =
        PEM_write_PrivateKey(
            keyFile,
            key,
            nullptr,
            nullptr,
            0,
            nullptr,
            nullptr
        ) == 1;


    const bool certWritten =
        PEM_write_X509(
            certFile,
            certificate
        ) == 1;


    std::fclose(
        keyFile
    );


    std::fclose(
        certFile
    );


    X509_free(
        certificate
    );


    EVP_PKEY_free(
        key
    );


    return
        keyWritten &&
        certWritten;
}


/*
 * =============================================================
 * MOBILE URL
 * =============================================================
 */

std::string
MobileNetwork::buildMobileUrl(
    const std::string& ipAddress,
    std::uint16_t port,
    const std::string& sourceId
)
{
    return
        "https://" +
        ipAddress +
        ":" +
        std::to_string(
            port
        ) +
        "/mobile?source=" +
        sourceId;
}