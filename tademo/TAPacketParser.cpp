#include "TAPacketParser.h"
#include "DPlayPacket.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include "HexDump.h"
#include "TPacket.h"
#include "Watchdog.h"

#include <QtCore/qdebug.h>

using namespace TADemo;

TAPacketParser::TAPacketParser() :
    m_progressTicks(0u)
{ }

void TAPacketParser::subscribe(TaPacketHandler* packetHandler)
{
    if (packetHandler != NULL)
    {
        m_packetHandlers.push_back(packetHandler);
    }
}

std::set<SubPacketCode> TAPacketParser::parseGameData(bool isLocalSource, const char *data, int len)
{
    Watchdog wd("TAPacketParser::parseGameData", 100);
    const DPHeader *header = NULL;
    m_parsedSubPacketCodes.clear();
    for (const char *ptr = data; ptr < data + len; ptr += header->size())
    {
        header = (const DPHeader*)ptr;
        if (!header->looksOk() && len > 8)
        {
            std::uint32_t id1 = *(std::uint32_t*)data;
            std::uint32_t id2 = *(std::uint32_t*)(data + 4);
            parseTaPacket(id1, id2, isLocalSource, data+8, len-8, "no dplay header");
            return m_parsedSubPacketCodes;
        }

        if (std::strncmp(header->actionstring, "play", 4) == 0)
        {
            parseDplayPacket(header, ptr, header->size());
        }
        else
        {
            parseTaPacket(*(std::uint32_t*)header->actionstring, 0, isLocalSource, ptr + sizeof(DPHeader), header->size() - sizeof(DPHeader), "with dplay header");
        }
    }
    return m_parsedSubPacketCodes;
}

std::uint32_t TAPacketParser::getProgressTicks()
{
    return m_progressTicks;
}

void TAPacketParser::parseDplayPacket(const DPHeader *header, const char *data, int len)
{
    Watchdog wd("TAPacketParser::parseDplayPacket", 100);
    switch (header->command)
    {
    case 0x0029:
        parseDplaySuperEnumReply(header, data, len);
        break;

    case 0x0008:    // DPSP_MSG_CREATEPLAYER
    case 0x0013:    // DPSP_MSG_ADDFORWARDREQUEST
    case 0x002e:    // DPSP_MSG_ADDFORWARD
    case 0x0038:    // DPSP_MSG_CREATEPLAYERVERIFY
        parseDplayCreateOrForwardPlayer(header, data, len);
        break;

    case 0x000b:
        parseDplayDeletePlayer(header, data, len);
        break;
    };
}

void TAPacketParser::parseDplaySuperEnumReply(const DPHeader *header, const char *data, int len)
{
    Watchdog wd("TAPacketParser::parseDplaySuperEnumReply", 100);
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
    DPSuperEnumPlayersReplyHeader* ephdr = (DPSuperEnumPlayersReplyHeader*)(header + 1);
    if (ephdr->packed_offset == 0)
    {
        return;
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

    DPSuperPackedPlayer* player = (DPSuperPackedPlayer*)(data + 0x4a - 0x36 + ephdr->packed_offset);

    for (unsigned n = 0u; n < ephdr->player_count; ++n)
    {
        char* ptr = (char*)player + player->size;
        ptr += 4;  //  systemPlayerIdOrDirectPlayVersion

        std::string playerName;
        if (player->info.haveShortName)
        {
            playerName = getCWStr((std::uint16_t*)ptr);
            ptr = (char*)skipCWStr((std::uint16_t*)ptr);
        }
        if (player->info.haveLongName)
        {
            playerName = getCWStr((std::uint16_t*)ptr);
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
        for (auto handler : m_packetHandlers)
        {
            handler->onDplaySuperEnumPlayerReply(player->id, playerName, addr, addr + 1);
        }

        player = (DPSuperPackedPlayer*)(addr+2);
    }
}

void TAPacketParser::parseDplayCreateOrForwardPlayer(const DPHeader *header, const char *data, int len)
{
    Watchdog wd("TAPacketParser::parseDplayCreateOrForwardPlayer", 100);
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
    DPForwardOrCreateRequest* req = (DPForwardOrCreateRequest*)(header + 1);
    if (req->player.service_provider_data_size == 0)
    {
        return;
    }

    std::string playerName;
    if (req->player.short_name_length > 0)
    {
        playerName = getCWStr((std::uint16_t*)&req->sentinel);
    }
    if (req->player.long_name_length > 0)
    {
        playerName = getCWStr((std::uint16_t*)(&req->sentinel + req->player.short_name_length));
    }

    DPAddress* addr = (DPAddress*)(&req->sentinel + req->player.short_name_length + req->player.long_name_length);
    DPAddress *addrTcp = req->player.service_provider_data_size == sizeof(DPAddress) ? addr : NULL;
    DPAddress *addrUdp = req->player.service_provider_data_size == 2*sizeof(DPAddress) ? (addr+1) : NULL;
    for (auto handler : m_packetHandlers)
    {
        handler->onDplayCreateOrForwardPlayer(header->command, req->player_id, playerName, addrTcp, addrUdp);
    }
}

void TAPacketParser::parseDplayDeletePlayer(const DPHeader *header, const char *data, int len)
{
    Watchdog wd("TAPacketParser::parseDplayDeletePlayer", 100);
    struct DPDeleteRequest
    {
        std::uint32_t id_to;
        std::uint32_t player_id;
        std::uint32_t group_id;
        std::uint32_t create_offset;
        std::uint32_t password_offset;
        DPPackedPlayer player;
        char sentinel;
    };
    DPDeleteRequest* req = (DPDeleteRequest*)(header + 1);
    for (auto handler : m_packetHandlers)
    {
        handler->onDplayDeletePlayer(req->player_id);
    }
}

void TAPacketParser::parseTaPacket(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, bool isLocalSource, const char *_payload, int _payloadSize, const std::string & context)
{
    Watchdog wd(QString("TAPacketParser::parseTaPacket sz=") + QString::number(_payloadSize), 100);
    if (otherDplayId == 0u && m_taDuplicateDetection.isLikelyDuplicate(sourceDplayId, otherDplayId, _payload, _payloadSize))
    {
        return;
    }

    bytestring payload((const std::uint8_t*)_payload, _payloadSize);
    {
        Watchdog wd2("TAPacketParser::parseTaPacket decrypt", 100);
        std::uint16_t checksum[2];
        TPacket::decrypt(payload, 0u, checksum[0], checksum[1]);
        if (checksum[0] != checksum[1])
        {
            qWarning() << "[TAPacketParser::parseTaPacket] checksum mismatch! context=" << QString::fromStdString(context);
            return;
        }
    }

    if (PacketCode(payload[0]) == PacketCode::COMPRESSED)
    {
        Watchdog wd3("TAPacketParser::parseTaPacket decompress", 100);
        payload = TPacket::decompress(payload, 3);
        if (payload[0] != 0x03)
        {
            qWarning() << "[TAPacketParser::parseTaPacket] decompression ran out of bytes! context=" << QString::fromStdString(context);
        }
    }

    std::vector<bytestring> subpaks;
    {
        Watchdog wd3("TAPacketParser::parseTaPacket unsmartpak", 100);
        subpaks = TPacket::unsmartpak(payload, true, true);
    }

    for (const TADemo::bytestring& s : subpaks)
    {
        unsigned expectedSize = TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            qWarning() << "[TAPacketParser::parseTaPacket] unknown subpacket. packet code" << QString::number(s[0], 16) << "expected size " << QString::number(expectedSize, 16) << "actual size " << QString::number(s.size(), 16) << "context=" << QString::fromStdString(context);
            std::ostringstream ss;
            ss << "  _payload:\n";
            TADemo::StrHexDump(_payload, _payloadSize, ss);
            qWarning() << ss.str().c_str();
            return;
        }
        m_parsedSubPacketCodes.insert(SubPacketCode(s[0]));

        switch (SubPacketCode(s[0]))
        {
        case SubPacketCode::UNIT_STAT_AND_MOVE_2C:
        {
            std::uint32_t tick = *(std::uint32_t*)(&s[3]);
            m_progressTicks = std::max(tick, m_progressTicks);
        }
        default:
            break;
        };
    }

    for (auto handler : m_packetHandlers)
    {
        handler->onTaPacket(sourceDplayId, otherDplayId, isLocalSource, _payload, _payloadSize, subpaks);
    }
}
