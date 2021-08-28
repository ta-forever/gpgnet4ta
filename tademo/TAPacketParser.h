#pragma once

#include <cinttypes>
#include <map>

#include "TPacket.h"
#include "DuplicateDetection.h"

namespace TADemo
{

    struct DPHeader;

    class TAPacketParser
    {
        std::vector<TaPacketHandler*> m_packetHandlers;

        // TA sends the same packets once to each player
        // The parser sees each copy
        // We want to detect the copy so we don't waste time decoding it
        // and echoing resulting GameEventData multiple times
        DuplicateDetection m_taDuplicateDetection;

        std::set<SubPacketCode> m_parsedSubPacketCodes;
        std::uint32_t m_progressTicks;

    public:
        TAPacketParser();

        virtual void subscribe(TaPacketHandler* packetHandler);
        virtual std::set<SubPacketCode> parseGameData(bool isLocalSource, const char *data, int len);
        virtual std::uint32_t getProgressTicks();

    private:
        virtual void parseDplayPacket(const DPHeader *header, const char *data, int len);
        virtual void parseTaPacket(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, bool isLocalSource, const char *data, int len, const std::string &context);
        virtual void parseDplaySuperEnumReply(const DPHeader *header, const char *data, int len);
        virtual void parseDplayCreateOrForwardPlayer(const DPHeader *header, const char *data, int len);
        virtual void parseDplayDeletePlayer(const DPHeader *header, const char *data, int len);
    };
}
