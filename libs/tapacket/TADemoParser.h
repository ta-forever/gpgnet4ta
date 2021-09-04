#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <thread>
#include <chrono>

#include "TPacket.h"
#include "TADemoRecords.h"

namespace tapacket
{
    class RecordReader
    {
        enum class State { READ_RECLEN1, READ_RECLEN2, READ_RECORD };
        State m_state;

        std::uint8_t m_recordLength[2];

        bytestring m_readBuffer;
        std::streamsize m_bytesRead;

    public:

        RecordReader() : m_bytesRead(0u), m_state(State::READ_RECLEN1) {}

        struct DataNotReadyException { };

        // returns a completed record or throws DataNotReadyException
        bytestring operator()(std::istream *is);
    };


    class DemoParser
    {
        RecordReader m_recordReader;
        std::istream *m_is;

        // remember state for benefit of re-entry
        std::unique_ptr<Header> m_header;
        std::unique_ptr<ExtraHeader> m_extraHeader;
        unsigned m_numExtraSectorsRead = 0u;
        unsigned m_numPlayersRead = 0u;
        unsigned m_numPlayerStatusMessagesRead = 0u;
        unsigned m_numUnitDataRead = 0u;
        unsigned m_numPacketsRead = 0u;

        // running count of how many times we reached EOF and then later received more data
        // as a mechanism to distrimincate between a live game and one where user just copied a .tad into the demo folder
        int m_numTimesNewDataReceived;

    public:

        DemoParser();

        virtual bool parse(std::istream *is, unsigned maxPaksToLoad);
        virtual int numTimesNewDataReceived() const;

        virtual void handle(const Header &header) = 0;
        virtual void handle(const Player &player, int n, int ofTotal) = 0;
        virtual void handle(const ExtraSector &es, int n, int ofTotal) = 0;
        virtual void handle(const PlayerStatusMessage &msg, std::uint32_t dplayid, int n, int ofTotal) = 0;
        virtual void handle(const UnitData &unitData) = 0;
        virtual void handle(const Packet &packet, const std::vector<bytestring> &unpaked, std::size_t n) = 0;

    private:

        virtual void doParse(unsigned maxPaksToLoad);

        virtual void load(Header &);
        virtual void load(ExtraHeader &);
        virtual void load(ExtraSector &);
        virtual void load(Player &);
        virtual void load(PlayerStatusMessage &);
        virtual void load(UnitData &);
        virtual void load(Packet &);
    };

}