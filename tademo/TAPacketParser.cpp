#include "TAPacketParser.h"
#include "DPlayPacket.h"

#include <algorithm>
#include <fstream>

#include "HexDump.h"
#include "TPacket.h"

using namespace TADemo;

TAPacketParser::TAPacketParser(TaPacketHandler *packetHandler) :
m_packetHandler(packetHandler)
{ }

void TAPacketParser::parseGameData(const char *data, int len)
{
    const DPHeader *header = NULL;
    for (const char *ptr = data; ptr < data + len; ptr += header->size())
    {
        header = (const DPHeader*)ptr;
        if (!header->looksOk() && len > 8)
        {
            std::uint32_t id1 = *(std::uint32_t*)data;
            std::uint32_t id2 = *(std::uint32_t*)(data + 4);
            parseTaPacket(id1, id2, data+8, len-8);
            return;
        }

        if (std::strncmp(header->actionstring, "play", 4) == 0)
        {
            parseDplayPacket(header, ptr, header->size());
        }
        else
        {
            parseTaPacket(*(std::uint32_t*)header->actionstring, 0, ptr + sizeof(DPHeader), header->size() - sizeof(DPHeader));
        }
    }
}

void TAPacketParser::parseDplayPacket(const DPHeader *header, const char *data, int len)
{
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
        m_packetHandler->onDplaySuperEnumPlayerReply(player->id, playerName, addr, addr + 1);

        player = (DPSuperPackedPlayer*)(addr+2);
    }
}

void TAPacketParser::parseDplayCreateOrForwardPlayer(const DPHeader *header, const char *data, int len)
{
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
    m_packetHandler->onDplayCreateOrForwardPlayer(header->command, req->player_id, playerName, addrTcp, addrUdp);
}

void TAPacketParser::parseDplayDeletePlayer(const DPHeader *header, const char *data, int len)
{
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
    m_packetHandler->onDplayDeletePlayer(req->player_id);
}

void TAPacketParser::parseTaPacket(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const char *_payload, int _payloadSize)
{
    if (otherDplayId == 0u && m_taDuplicateDetection.isLikelyDuplicate(sourceDplayId, otherDplayId, _payload, _payloadSize))
    {
        return;
    }

    bytestring payloadDecrypted = TPacket::decrypt(bytestring((std::uint8_t*)_payload, _payloadSize));

    bytestring payloadDecompressed;
    if (PacketCode(payloadDecrypted[0]) == PacketCode::COMPRESSED)
    {
        payloadDecompressed = TPacket::decompress(payloadDecrypted);
    }
    else
    {
        payloadDecompressed = payloadDecrypted;
    }

    for (const bytestring &s : TPacket::unsmartpak(payloadDecompressed, 3))
    {
        unsigned expectedSize = TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            std::cout << "[TAPacketParser::parseTaPacket] subpacket error. packet code " << std::hex << unsigned(s[0]) << ", expected size " << expectedSize << ", actual size " << s.size() << '\n';
            std::cout << "extracted from _payload:\n";
            TADemo::HexDump(_payload, _payloadSize, std::cout);
            continue;
        }

        switch (SubPacketCode(s[0]))
        {
        case SubPacketCode::STATUS:
            {
                std::string mapName = (const char*)(&s[1]);
                std::uint16_t maxUnits = *(std::uint16_t*)(&s[0xa6]);
                bool isAI = s[0x95] == 2;
                bool isWatcher = (s[0x9c] & 0x40) != 0;
                unsigned armOrCore = s[0x96];
                bool cheats = (s[0x9d] & 0x20) != 0;
                unsigned playerSlotNumber = s[0x97];
                Side playerSide = isWatcher ? Side::WATCH : Side(armOrCore);
                if (playerSlotNumber < 10)
                {
                    m_packetHandler->onStatus(sourceDplayId, mapName, maxUnits, playerSlotNumber, playerSide, isAI, cheats);
                }
            }
            break;

        case SubPacketCode::CHAT:
            {
                std::string chat = (const char*)(&s[1]);
                m_packetHandler->onChat(sourceDplayId, chat);
            }
            break;

        case SubPacketCode::UNIT_DIED:
            {
                std::uint16_t unitId = *(std::uint16_t*)(&s[1]);
                m_packetHandler->onUnitDied(sourceDplayId, unitId);
            }
            break;

        case SubPacketCode::REJECT:
            {
                std::uint32_t rejectedDplayId = *(std::uint32_t*)(&s[1]);
                m_packetHandler->onRejectOther(sourceDplayId, rejectedDplayId);
            }
            break;

        case SubPacketCode::TICK:
            {
                std::uint32_t tick = *(std::uint32_t*)(&s[3]);
                m_packetHandler->onGameTick(sourceDplayId, tick);
            }
            break;
        };
    }
}
