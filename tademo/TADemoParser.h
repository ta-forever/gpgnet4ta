#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <thread>
#include <chrono>

#include "TPacket.h"

namespace TADemo
{
    enum class Side { ARM = 0, CORE = 1, WATCH = 2 };

    void HexDump(const void* _buff, std::size_t size, std::ostream& s);

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

        std::uint8_t color:8;
        Side side:8;
        std::uint8_t number:8;
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

    class RecordReader
    {
        bytestring m_readBuffer;
        std::streamsize m_bytesRead;

    public:

        RecordReader() : m_bytesRead(0u) { }

        struct DataNotReadyException { };

        // returns a completed record or throws DataNotReadyException
        bytestring operator()(std::istream *is);

    private:
        void getBytes(std::istream *is);
    };


    class Parser
    {
        RecordReader m_recordReader;
        std::istream *m_is;

        // remember state for benefit of re-entry
        std::unique_ptr<Header> m_header;
        std::unique_ptr<ExtraHeader> m_extraHeader;
        int m_numExtraSectorsRead;
        int m_numPlayersRead;
        int m_numPlayerStatusMessagesRead;
        int m_numUnitDataRead;
        int m_numPacketsRead;

        // running count of how many times we reached EOF and then later received more data
        // as a mechanism to distrimincate between a live game and one where user just copied a .tad into the demo folder
        int m_numTimesNewDataReceived;

    public:

        Parser();

        virtual bool parse(std::istream *is);
        virtual int numTimesNewDataReceived() const;

        virtual void handle(const Header &header) = 0;
        virtual void handle(const Player &player, int n, int ofTotal) = 0;
        virtual void handle(const ExtraSector &es, int n, int ofTotal) = 0;
        virtual void handle(const PlayerStatusMessage &msg, std::uint32_t dplayid, int n, int ofTotal) = 0;
        virtual void handle(const UnitData &unitData) = 0;
        virtual void handle(const Packet &packet, const std::vector<bytestring> &unpaked, std::size_t n) = 0;

    private:

        virtual void doParse();

        virtual void load(Header &);
        virtual void load(ExtraHeader &);
        virtual void load(ExtraSector &);
        virtual void load(Player &);
        virtual void load(PlayerStatusMessage &);
        virtual void load(UnitData &);
        virtual void load(Packet &);
    };

}