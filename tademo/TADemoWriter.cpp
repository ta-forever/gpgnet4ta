#include "TADemoWriter.h"

#include <algorithm>
#include <sstream>

using namespace TADemo;

TADemoWriter::TADemoWriter(std::ostream* o):
    m_output(o)
{ }

void TADemoWriter::writeRecord(const std::string& s)
{
    writeRecord(s.data(), s.size());
}

void TADemoWriter::writeRecord(const char* ptr, std::size_t size)
{
    std::uint16_t size16 = 2u + size;
    if (size16 != 2u + size)
    {
        throw std::runtime_error("Oversize record!");
    }
    m_output->write((char*)&size16, sizeof(size16));
    m_output->write(ptr, size);
}

void TADemoWriter::write(const Header& h)
{
    std::ostringstream ss;
    ss.write(h.magic, sizeof(h.magic));
    ss.write((char*)&h.version, sizeof(h.version));
    ss.write((char*)&h.numPlayers, sizeof(h.numPlayers));
    ss.write((char*)&h.maxUnits, sizeof(h.maxUnits));
    ss.write(h.mapName.c_str(), std::min(h.mapName.size(), std::size_t(64u)));
    writeRecord(ss.str());
}

void TADemoWriter::write(const ExtraHeader& eh)
{
    writeRecord((char*)&eh.numSectors, sizeof(eh.numSectors));
}

void TADemoWriter::write(const Player& p)
{
    std::ostringstream ss;
    ss.put(p.color);
    ss.put(p.side);
    ss.put(p.number);
    ss.write(p.name.c_str(), std::min(p.name.size(), std::size_t(16u)));
    writeRecord(ss.str());
}

void TADemoWriter::write(const PlayerStatusMessage& s)
{
    std::ostringstream ss;
    ss.put(s.number);
    ss.write((char*)s.statusMessage.data(), s.statusMessage.size());
    writeRecord(ss.str());
}

void TADemoWriter::write(const UnitData& u)
{
    writeRecord((char*)u.unitData.data(), u.unitData.size());
}

void TADemoWriter::write(const Packet& p)
{
    std::ostringstream ss;
    ss.write((char*)&p.time, sizeof(p.time));
    ss.write((char*)&p.sender, sizeof(p.sender));
    ss.write((char*)p.data.data(), p.data.size());
    writeRecord(ss.str());
}

void TADemoWriter::flush()
{
    m_output->flush();
}
