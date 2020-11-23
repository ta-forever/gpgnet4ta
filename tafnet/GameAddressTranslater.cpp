#include "GameAddressTranslater.h"

#include <cstring>
#include <sstream>
#include <iostream>

using namespace tafnet;

static std::uint16_t NetworkByteOrder(std::uint16_t x)
{
    return (x >> 8) | (x << 8);
}

static std::uint32_t NetworkByteOrder(std::uint32_t x)
{
    return (x << 24) | ((x & 0xff00) << 8) | ((x & 0xff0000) >> 8) | ((x & 0xff000000) >> 24);
}

static const std::uint16_t* skipCWStr(const std::uint16_t* p)
{
    while (*p++ != 0);
    return p;
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
        size() >= sizeof(DPHeader) &&
        (token() == 0xfab || token() == 0xcab || token() == 0xbab) &&
        address.family == 2; // AF_INET
        //dialect == 0x0e && // dplay 9
        //std::memcmp(address.pad, "\0\0\0\0\0\0\0\0", 8) == 0;
}


GameAddressTranslater::GameAddressTranslater(std::uint32_t _replyAddress, const std::uint16_t _replyPorts[]) :
    replyAddress(_replyAddress),
    translatePlayerSPA([] (DPAddress &, int) { return false; })
{
    replyPorts[0] = _replyPorts[0];
    replyPorts[1] = _replyPorts[1];
}


GameAddressTranslater::GameAddressTranslater(
    std::uint32_t _replyAddress, const std::uint16_t _replyPorts[],
    const TranslatePlayerSPA &_translate) :
    replyAddress(_replyAddress),
    translatePlayerSPA(_translate)
{
    replyPorts[0] = _replyPorts[0];
    replyPorts[1] = _replyPorts[1];
}

void GameAddressTranslater::operator()(char* buf, int len) const
{
    for (char* ptr = buf; ptr < buf + len;) {
        DPHeader* hdr = (DPHeader*)ptr;
        if (translateHeader(ptr, hdr->size()))
        {
            translateSuperEnumPlayersReply(ptr, hdr->size()) ||
                translateForwardOrCreateRequest(ptr, hdr->size());
        }
        ptr += hdr->size();
    }
}

bool GameAddressTranslater::translateHeader(char* buf, int len) const
{
    DPHeader* dp = (DPHeader*)buf;
    if (dp->looksOk())
    {
        translateReplyAddress(dp->address, 0);
        return true;
    }
    else
    {
        return false;
    }
}

void GameAddressTranslater::translateReplyAddress(DPAddress &address, int index) const
{
#ifdef _DEBUG
    std::cout << "[GameAddressTranslater:translateAddress] " << address.debugString();
#endif
    address.address(replyAddress);
    address.port(replyPorts[index]);
#ifdef _DEBUG
    std::cout << " -> " << address.debugString() << std::endl;;
#endif
}


bool GameAddressTranslater::translateForwardOrCreateRequest(char* buf, int len) const
{
    DPHeader* dp = (DPHeader*)buf;
    if (dp->command != 0x2e && // DPSP_MSG_ADDFORWARD
        dp->command != 0x13 && // DPSP_MSG_ADDFORWARDREQUEST
        dp->command != 0x08 && // DPSP_MSG_CREATEPLAYER
        dp->command != 0x38    // DPSP_MSG_CREATEPLAYERVERIFY
        )
    {
        return false;
    }
    struct DPForwardOrCreateRequest
    {
        std::uint32_t id_to;
        std::uint32_t player_id;
        std::uint32_t group_id;
        std::uint32_t create_offset;
        std::uint32_t password_offset;
        DPPackedPlayer player;
        char sentinel;
    };
    DPForwardOrCreateRequest* req = (DPForwardOrCreateRequest*)(dp + 1);
    if (req->player.service_provider_data_size == 0)
    {
        return false;
    }
    DPAddress* addr = (DPAddress*)(&req->sentinel + req->player.short_name_length + req->player.long_name_length);
    for (unsigned n = 0u; n * sizeof(DPAddress) < req->player.service_provider_data_size; ++n, ++addr)
    {
        if (!translatePlayerSPA(*addr, n))
        {
            translateReplyAddress(*addr, n);
        }
    }
    return true;
}

bool GameAddressTranslater::translateSuperEnumPlayersReply(char* buf, int len) const
{
    DPHeader* dp = (DPHeader*)buf;
    if (dp->command != 0x0029) // Super Enum Players Reply
    {
        return false;
    }

    struct DPSuperEnumPlayersReplyHeader
    {
        std::uint32_t player_count;
        std::uint32_t group_count;
        std::uint32_t packed_offset;
        std::uint32_t shortcut_count;
        std::uint32_t description_offset;
        std::uint32_t name_offset;
        std::uint32_t password_offset;
    };
    DPSuperEnumPlayersReplyHeader* ephdr = (DPSuperEnumPlayersReplyHeader*)(dp + 1);
    if (ephdr->packed_offset == 0)
    {
        return true;
    }

    struct DPSuperPackedPlayerInfo
    {
        unsigned haveShortName : 1;
        unsigned haveLongName : 1;
        unsigned serviceProvideDataLengthBytes : 2;
        unsigned playerDataLengthBytes : 2;
        unsigned pad : 26;
    };
    struct DPSuperPackedPlayer
    {
        std::uint32_t size;
        std::uint32_t flags;
        std::uint32_t id;
        DPSuperPackedPlayerInfo info;
    };

    DPSuperPackedPlayer* player = (DPSuperPackedPlayer*)(buf + 0x4a - 0x36 + ephdr->packed_offset);
    for (unsigned n = 0u; n < ephdr->player_count; ++n)
    {
        char* ptr = (char*)player + player->size;
        ptr += 4;  //  systemPlayerIdOrDirectPlayVersion
        if (player->info.haveShortName)
        {
            ptr = (char*)skipCWStr((std::uint16_t*)ptr);
        }
        if (player->info.haveLongName)
        {
            ptr = (char*)skipCWStr((std::uint16_t*)ptr);
        }
        if (player->info.playerDataLengthBytes == 1)
        {
            int len = *(std::uint8_t*)ptr;
            ptr += 1 + len;
        }
        if (player->info.playerDataLengthBytes == 2)
        {
            int len = *(std::uint16_t*)ptr;
            ptr += 2 + len;
        }
        if (player->info.serviceProvideDataLengthBytes == 1)
        {
            int len = *(std::uint8_t*)ptr;
            ptr += 1;
        }
        if (player->info.serviceProvideDataLengthBytes == 2)
        {
            int len = *(std::uint16_t*)ptr;
            ptr += 2;
        }
        DPAddress* addr = (DPAddress*)ptr;
        for (int i = 0; i < 2; ++i, ++addr)
        {
            if (!translatePlayerSPA(*addr, i))
            {
                translateReplyAddress(*addr, i);
            }
        }
        player = (DPSuperPackedPlayer*)(addr);
    }
    return true;
}
