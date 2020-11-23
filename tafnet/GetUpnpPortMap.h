#pragma once

#include <map>
#include <cinttypes>

namespace tafnet
{
    std::uint16_t GetUpnpPortMap(std::uint16_t _extPort, const char *TCP_or_UDP);
    void GetUpnpPortMaps(const char *internalClient, std::map<std::uint16_t, std::uint16_t> &portMapsTcp, std::map<std::uint16_t, std::uint16_t> &portMapsUdp);
}
