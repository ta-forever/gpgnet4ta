#pragma once

#include <cstdint>
#include <string>

#include "TPacket.h"  // bytestring

namespace tapacket
{
    struct Header
    {
        char magic[8];              // "TA Demo\0";
        std::uint16_t version;      // 99b2 = 5
        std::uint8_t numPlayers;
        std::uint16_t maxUnits;
        std::string mapName;        // upto 64
    };

    struct ExtraHeader
    {
        std::uint32_t numSectors;
    };

    struct ExtraSector
    {
        enum TypeCode {
            COMMENTS = 1,
            CHAT = 2,
            RECORDER_VERSION = 3,
            DATE = 4,
            RECORDER_CONTEXT = 5,
            PLAYER_ADDR = 6,
            MOD_ID = 7
        };

        std::uint32_t sectorType;
        bytestring data;           // upto 200000
    };

    struct Player
    {
        std::uint8_t color : 8;
        std::int8_t side : 8;
        std::uint8_t number : 8;      // beware not consistent across all players' versions of demo
        std::string name;           // upto 64
        const char* getSide() const
        {
            switch (side) {
            case 0: return "ARM";
            case 1: return "CORE";
            case 2: return "WATCH";
            default: return "ARM";
            };
        }
    };

    struct PlayerStatusMessage
    {
        std::uint8_t number;
        bytestring statusMessage;      // i TA net format // upto 512
    };

    struct UnitData
    {
        bytestring unitData;           // alla $1a (subtyp 2,3) sända mellan player 1 och 2 ,efter varandra i okomprimerat format
                                        // all $ 1a (subtype 2,3) sent between players 1 and 2, one after the other in uncompressed format
                                        // upto 10000
    };

    struct Packet
    {
        std::uint16_t time;             // tid sedan senaste paket i ms / time since last package in ms
        std::uint8_t sender;
        bytestring data;              // upto 2048
    };
}