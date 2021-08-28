#pragma once

#include <cstdint>
#include <string>

#include "TPacket.h"  // bytestring

namespace TADemo
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
        std::uint16_t sectorType;
        bytestring data;           // upto 200000
    };

    struct Player
    {
        std::uint8_t color : 8;
        std::int8_t side : 8;
        std::uint8_t number : 8;      // beware not consistent across all players' versions of demo
        std::string name;           // upto 64
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