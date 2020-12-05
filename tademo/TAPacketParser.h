#pragma once

#include <cinttypes>
#include <map>

#include "TPacket.h"

namespace TADemo
{

    struct DPHeader;

    class TAPacketParser
    {
        bool m_dropDuplicateTaMessages;
        std::map<std::uint32_t,std::string> m_lastTaPacket; // keyed by source dplayid
        TaPacketHandler *m_packetHandler;

    public:
        TAPacketParser(TaPacketHandler *packetHandler, bool dropDuplicateTaMessages);

        virtual void parseGameData(const char *data, int len);

    private:
        virtual void parseDplayPacket(const DPHeader *header, const char *data, int len);
        virtual void parseTaPacket(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const char *data, int len);
        virtual void parseDplaySuperEnumReply(const DPHeader *header, const char *data, int len);
        virtual void parseDplayCreateOrForwardPlayer(const DPHeader *header, const char *data, int len);
        virtual void parseDplayDeletePlayer(const DPHeader *header, const char *data, int len);
    };
}
