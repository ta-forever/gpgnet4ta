#include <ctype.h>
#include <iostream>
#include <iomanip>
#include <cctype>
#include <sstream>

#include "TADemoParser.h"
#include "TPacket.h"

namespace TADemo
{

    void RecordReader::getBytes(std::istream *is)
    {
        std::uint8_t *ptr = &m_readBuffer[m_bytesRead];

        is->clear();
        is->read((char*)ptr, m_readBuffer.size() - m_bytesRead);
        m_bytesRead += is->gcount();

        if (m_bytesRead != m_readBuffer.size())
        {
            throw DataNotReadyException();
        }
    }

    bytestring RecordReader::operator()(std::istream *is)
    {
        //std::cout << std::dec << "rr " << m_bytesRead << '/' << m_readBuffer.size() << '.';
        bytestring record;

        if (m_readBuffer.empty())
        {
            std::streamsize pos = is->tellg();
            std::uint16_t length = 0u;
            is->clear();
            is->read((char*)&length, 2);
            if (is->gcount() == 2)
            {
                //std::cout << "got len=" << int(length) << '.';
                if (length > 16384 || length <= 2)
                {
                    std::ostringstream ss;
                    ss << "unrealistic record length " << length << " at position " << pos;
                    throw std::runtime_error(ss.str());
                }
                length -= 2;
                m_readBuffer.resize(length);
                m_bytesRead = 0u;
            }
            else if (is->gcount() == 0)
            {
                throw DataNotReadyException();
            }
            else
            {
                throw std::runtime_error("expected to be able to read 2-byte record length in one go!");
            }
        }

        if (!m_readBuffer.empty())
        {
            getBytes(is);
            //std::cout << "got buf=" << m_bytesRead << '/' << m_readBuffer.size() << '.';
            record = m_readBuffer;
            m_readBuffer.clear();
            m_bytesRead = 0u;
        }
        //std::cout << std::endl;
        return record;
    }

    void Parser::load(Header &h)
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

    void Parser::load(ExtraHeader &eh)
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

    void Parser::load(ExtraSector &es)
    {
        bytestring data = m_recordReader(m_is);
        std::memcpy(&es.sectorType, &data[0], sizeof(es.sectorType));
        es.data = data.substr(4);
    }

    void Parser::load(Player &p)
    {
        bytestring data = m_recordReader(m_is);
        p.color = data[0];
        p.side = data[1];
        p.number = data[2];
        p.name = (char*)data.substr(3).c_str();
    }

    void Parser::load(PlayerStatusMessage &msg)
    {
        bytestring data = m_recordReader(m_is);
        msg.number = data[0];
        msg.statusMessage = TPacket::decompress(TPacket::decrypt(data.substr(1))).substr(7);
    }

    void Parser::load(UnitData &ud)
    {
        ud.unitData = m_recordReader(m_is);
    }

    void Parser::load(Packet &p)
    {
        bytestring data = m_recordReader(m_is);
        std::memcpy(&p.time, &data[0], sizeof(p.time));
        p.sender = data[2];
        p.data = data.substr(3);
    }

    bool Parser::parse(std::istream *is)
    {
        m_is = is;
        if (!m_is)
        {
            return false;
        }

        try
        {
            doParse();
            return true;
        }
        catch (RecordReader::DataNotReadyException &)
        {
            return false;
        }
    }

    void Parser::doParse()
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

        for (;; ++m_numPacketsRead)
        {
            Packet p;
            load(p);
            std::vector<bytestring> unpacked = TPacket::unsmartpak(p.data, m_header->version);
            handle(p, unpacked, m_numPacketsRead);
        }
    }

} // namespace
