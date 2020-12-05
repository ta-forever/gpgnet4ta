#include "DPlayPacket.h"
#include <sstream>

using namespace TADemo;

const std::uint16_t* TADemo::skipCWStr(const std::uint16_t* p)
{
    while (*p++ != 0);
    return p;
}

std::string TADemo::getCWStr(const std::uint16_t* p)
{
    std::ostringstream ss;
    for (; *p != 0u; ++p)
    {
        ss << char(*p & 0xff);
    }
    return ss.str();
}

static std::uint16_t NetworkByteOrder(std::uint16_t x)
{
    return (x >> 8) | (x << 8);
}

static std::uint32_t NetworkByteOrder(std::uint32_t x)
{
    return (x << 24) | ((x & 0xff00) << 8) | ((x & 0xff0000) >> 8) | ((x & 0xff000000) >> 24);
}

std::string DPAddress::debugString() const
{
    std::ostringstream ss;
    const std::uint8_t* ipv4 = (const std::uint8_t*) &_ipv4;
    ss << unsigned(ipv4[0]) << '.' << unsigned(ipv4[1]) << '.' << unsigned(ipv4[2]) << '.' << unsigned(ipv4[3]) << ':' << port();
    return ss.str();
}

std::uint16_t DPAddress::port() const
{
    return NetworkByteOrder(_port);
}

void DPAddress::port(std::uint16_t port)
{
    _port = NetworkByteOrder(port);
}

std::uint32_t DPAddress::address() const
{
    return NetworkByteOrder(_ipv4);
}

void DPAddress::address(std::uint32_t addr)
{
    _ipv4 = NetworkByteOrder(addr);
}

unsigned DPHeader::size() const
{
    return size_and_token & 0x000fffff;
}

unsigned DPHeader::token() const
{
    return size_and_token >> 20;
}

bool DPHeader::looksOk() const
{
    return
        address.family == 2 && // AF_INET
        (token() == 0xfab || token() == 0xcab || token() == 0xbab) &&
        size() >= sizeof(DPHeader)
        ;
    //dialect == 0x0e && // dplay 9
    //std::memcmp(address.pad, "\0\0\0\0\0\0\0\0", 8) == 0;
}
