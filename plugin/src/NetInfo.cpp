#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "NetInfo.h"

#include <algorithm>

namespace NetInfo
{

std::vector<std::string> getLocalAddresses()
{
    std::vector<std::pair<int, std::string>> ranked;

    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    IP_ADAPTER_ADDRESSES* adapters = nullptr;
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        buffer.resize (size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.data());
        const auto result = GetAdaptersAddresses (AF_UNSPEC, flags, nullptr, adapters, &size);

        if (result == NO_ERROR)
            break;

        adapters = nullptr;

        if (result != ERROR_BUFFER_OVERFLOW)
            return {};
    }

    for (auto* a = adapters; a != nullptr; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;

        // A phone hotspot can be IPv6-only upstream, so a gateway of either family counts.
        int score = 0;

        if (a->FirstGatewayAddress != nullptr)       score += 4;
        if ((a->Flags & IP_ADAPTER_DHCP_ENABLED) != 0) score += 2;
        if (a->IfType == IF_TYPE_IEEE80211)          score += 1;

        // A USB-tethered phone is plugged in for exactly this, so its network wins over Wi-Fi.
        const std::wstring description (a->Description != nullptr ? a->Description : L"");

        if (description.find (L"Remote NDIS") != std::wstring::npos
            || description.find (L"Apple Mobile Device Ethernet") != std::wstring::npos)
            score += 8;

        for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
        {
            if (u->Address.lpSockaddr->sa_family != AF_INET)
                continue;

            char text[INET_ADDRSTRLEN] = {};
            auto* in = reinterpret_cast<sockaddr_in*> (u->Address.lpSockaddr);
            inet_ntop (AF_INET, &in->sin_addr, text, sizeof (text));
            const std::string ip (text);

            if (ip.rfind ("127.", 0) == 0 || ip.rfind ("169.254.", 0) == 0)
                continue;

            ranked.emplace_back (score, ip);
        }
    }

    std::stable_sort (ranked.begin(), ranked.end(),
                      [] (const auto& x, const auto& y) { return x.first > y.first; });

    std::vector<std::string> result;

    for (auto& r : ranked)
        result.push_back (r.second);

    return result;
}

} // namespace NetInfo
