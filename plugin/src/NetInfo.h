#pragma once

#include <string>
#include <vector>

namespace NetInfo
{
    /** IPv4 addresses a phone could reach this machine on, most likely first
        (real networks with a gateway before virtual/host-only adapters). */
    std::vector<std::string> getLocalAddresses();
}
