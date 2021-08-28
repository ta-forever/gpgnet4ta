#pragma once

#include "TADemoRecords.h"

namespace tapacket
{

    class TADemoWriter
    {
    public:
        TADemoWriter(std::ostream*);

        void write(const Header&);
        void write(const ExtraHeader&);
        void write(const Player&);
        void write(const PlayerStatusMessage&);
        void write(const UnitData&);
        void write(const Packet&);
        void flush();

    private:
        std::ostream* m_output;

        void writeRecord(const std::string& s);
        void writeRecord(const char* ptr, std::size_t size);

    };

}