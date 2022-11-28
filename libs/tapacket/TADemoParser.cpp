
#include <algorithm>
#include <cstring>
#include <ctype.h>
#include <iostream>
#include <iomanip>
#include <cctype>
#include <sstream>

#include "taflib/HexDump.h"
#include "TADemoParser.h"
#include "TPacket.h"
#include "Logger.h"

using namespace tapacket;

bytestring RecordReader::operator()(std::istream *is)
{
    //std::cout << std::dec << "rr " << m_bytesRead << '/' << m_readBuffer.size() << '.';
    bytestring record;
    
    is->clear();
    is->tellg();    // yeah I don't know.  linux/wine doesn't work without it ...

    if (m_state == State::READ_RECLEN1)
    {
        is->read((char*)&m_recordLength[0], 1);
        if (is->gcount() == 1)
        {
            m_state = State::READ_RECLEN2;
        }
        else
        {
            is->clear();
            throw DataNotReadyException();
        }
    }
    if (m_state == State::READ_RECLEN2)
    {
        is->read((char*)&m_recordLength[1], 1);
        if (is->gcount() == 1)
        {
            std::uint16_t length = unsigned(m_recordLength[0]) | (unsigned(m_recordLength[1]) << 8);
            if (length > 32768 || length <= 2)
            {
                std::ostringstream ss;
                ss << "[RecordReader::operator()] unrealistic record length " << length << " at position " << is->tellg();
                throw std::runtime_error(ss.str());
            }
            length -= 2;
            m_readBuffer.resize(length);
            m_bytesRead = 0u;
            m_state = State::READ_RECORD;
        }
        else
        {
            is->clear();
            throw DataNotReadyException();
        }
    }
    if (m_state == State::READ_RECORD)
    {
        std::uint8_t *ptr = &m_readBuffer[(unsigned)m_bytesRead];
        is->read((char*)ptr, m_readBuffer.size() - m_bytesRead);
        m_bytesRead += is->gcount();
        if (m_bytesRead < m_readBuffer.size())
        {
            is->clear();
            throw DataNotReadyException();
        }
        record = m_readBuffer;
        m_readBuffer.clear();
        m_bytesRead = 0u;
        m_state = State::READ_RECLEN1;
    }

    return record;
}

DemoParser::DemoParser()
{ }

void DemoParser::load(Header &h)
{
    bytestring data = m_recordReader(m_is);
    std::memcpy(h.magic, &data[0], sizeof(h.magic));
    if (std::strcmp(h.magic, "TA Demo"))
    {
        std::ostringstream ss;
        ss << "invalid tad header:" << h.version;
        throw std::runtime_error(ss.str());
    }
    std::memcpy(&h.version, &data[8], sizeof(h.version));
    if (h.version < 3 || h.version > 5)
    {
        std::ostringstream ss;
        ss << "unsupported tad version:" << h.version;
        throw std::runtime_error(ss.str());
    }

    std::memcpy(&h.numPlayers, &data[10], sizeof(h.numPlayers));
    if (h.numPlayers > 10)
    {
        std::ostringstream ss;
        ss << "unrealistic number of players:" << h.numPlayers;
        throw std::runtime_error(ss.str());
    }

    std::memcpy(&h.maxUnits, &data[11], sizeof(h.maxUnits));
    if (h.maxUnits > 5000)
    {
        std::ostringstream ss;
        ss << "unrealistic max units:" << h.maxUnits;
        throw std::runtime_error(ss.str());
    }

    h.mapName = (char*)data.substr(13).c_str();
    if (h.mapName.size() > 64)
    {
        std::ostringstream ss;
        ss << "unrealistic size of mapname:" << h.mapName.size();
        throw std::runtime_error(ss.str());
    }
    for (auto ch : h.mapName)
    {
        if (!isprint(ch))
        {
            std::ostringstream ss;
            ss << "mapname contains unprintable characters:" << h.mapName;
            throw std::runtime_error(ss.str());
        }
    }
}

void DemoParser::load(ExtraHeader &eh)
{
    bytestring data = m_recordReader(m_is);
    std::memcpy(&eh.numSectors, &data[0], sizeof(eh.numSectors));
    if (eh.numSectors > 10000)
    {
        std::ostringstream ss;
        ss << "unrealistic number of extra sectors:" << eh.numSectors;
        throw std::runtime_error(ss.str());
    }
}

void DemoParser::load(ExtraSector &es)
{
    bytestring data = m_recordReader(m_is);
    std::memcpy(&es.sectorType, &data[0], sizeof(es.sectorType));
    es.data = data.substr(4);
}

void DemoParser::load(Player &p)
{
    bytestring data = m_recordReader(m_is);
    p.color = data[0];
    p.side = static_cast<std::int8_t>(data[1]);
    p.number = data[2];
    p.name = (char*)data.substr(3).c_str();
}

void DemoParser::load(PlayerStatusMessage &msg)
{
    bytestring data = m_recordReader(m_is);
    msg.number = data[0];
    std::uint16_t checks[2];
    TPacket::decrypt(data, 1u, checks[0], checks[1]);
    if (checks[0] != checks[1])
    {
        std::ostringstream ss;
        taflib::HexDump(data.data()+1, data.size()-1, ss);
        qWarning() << "[DemoParser::load PlayerStatusMessage] checksum error\n" << ss.str().c_str();
    }
    msg.statusMessage = TPacket::decompress(data.data()+1, data.size()-1, 3).substr(7);
}

void DemoParser::load(UnitData &ud)
{
    ud.unitData = m_recordReader(m_is);
}

void DemoParser::load(Packet &p)
{
    bytestring data = m_recordReader(m_is);
    std::memcpy(&p.time, &data[0], sizeof(p.time));
    p.sender = data[2];
    p.data = data.substr(3);
}

bool DemoParser::parse(std::istream *is, unsigned maxPaksToLoad)
{
    m_is = is;
    if (!m_is)
    {
        return false;
    }

    unsigned numPacketsRead = m_numPacketsRead;
    try
    {
        doParse(maxPaksToLoad);
    }
    catch (RecordReader::DataNotReadyException &)
    { }

    if (m_numPacketsRead > numPacketsRead)
    {
        ++m_numTimesNewDataReceived;
        return true;
    }
    else
    {
        return false;
    }
}

int DemoParser::numTimesNewDataReceived() const
{
    return m_numTimesNewDataReceived;
}

void DemoParser::doParse(unsigned maxPaksToLoad)
{
    if (!m_header)
    {
        Header header;
        load(header);
        handle(header);
        m_header.reset(new Header(header));
    }

    if (m_header->version > 4 )
    {
        if (!m_extraHeader)
        {
            ExtraHeader eh;
            load(eh);
            m_extraHeader.reset(new ExtraHeader(eh));
        }

        for (; m_numExtraSectorsRead < m_extraHeader->numSectors; ++m_numExtraSectorsRead)
        {
            ExtraSector es;
            load(es);
            if (es.sectorType == 6)
            {
                std::transform(es.data.begin(), es.data.end(), es.data.begin(),
                    [](char c) -> char { return c ^ 42; });
            }
            handle(es, m_numExtraSectorsRead, m_extraHeader->numSectors);
        }
    }

    for (; m_numPlayersRead < m_header->numPlayers; ++m_numPlayersRead)
    {
        Player p;
        load(p);
        handle(p, m_numPlayersRead, m_header->numPlayers);
    }

    for (; m_numPlayerStatusMessagesRead < m_header->numPlayers; ++m_numPlayerStatusMessagesRead)
    {
        PlayerStatusMessage msg;
        load(msg);

        std::uint32_t dplayid = *(std::uint32_t*)(&msg.statusMessage[0x91]);
        handle(msg, dplayid, m_numPlayerStatusMessagesRead, m_header->numPlayers);
    }

    for (; m_numUnitDataRead < 1; ++m_numUnitDataRead)
    {
        UnitData ud;
        load(ud);
        handle(ud);
    }

    for (unsigned n = 0u; n < maxPaksToLoad || maxPaksToLoad == 0u; ++n, ++m_numPacketsRead)
    {
        Packet p;
        load(p);
        const bool hasTimestamp = m_header->version == 3;
        const bool hasChecksum = false;
        std::vector<bytestring> unpacked = TPacket::unsmartpak(p.data, hasTimestamp, hasChecksum);
        handle(p, unpacked, m_numPacketsRead);
    }
}
