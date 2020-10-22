#pragma once

#include <string>
#include <cstdint>

namespace TADemo
{

    typedef std::basic_string<std::uint8_t> bytestring;

    class TPacket
    {
    protected:

    public:
        static bytestring decrypt(const bytestring &data);
        static bytestring decompress(const bytestring &data);
        static bytestring split2(bytestring &s, bool smartpak);

        static std::vector<bytestring> unsmartpak(const bytestring &c, unsigned version);

        // @param s string who's characters contain bits of an integer <char1>,<char2>,<char3>,<char4>,etc
        // @param start number of bits into the string from where the number starts.  eg 0 starts at LSB of char1.  8 starts at LSB of char2.
        // @param num number of bits to extract
        static unsigned bin2int(const bytestring &s, unsigned start, unsigned num);

    private:
        struct PacketHeader
        {
            std::uint8_t type;
            std::uint16_t check;
        };
    };

}
