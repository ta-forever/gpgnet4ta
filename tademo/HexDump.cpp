#include <cctype>
#include <iomanip>

#include "HexDump.h"

namespace TADemo
{

    void HexDump(const void* _buff, std::size_t size, std::ostream& s)
    {
        const unsigned char* buff = (const unsigned char*)_buff;
        for (std::size_t base = 0; base < size; base += 16)
        {
            s << std::setw(4) << std::hex << base << ": ";
            for (std::size_t ofs = 0; ofs < 16; ++ofs)
            {
                std::size_t idx = base + ofs;
                if (idx < size)
                {
                    unsigned byte = buff[idx] & 0x0ff;
                    s << std::setw(2) << std::hex << byte << ' ';
                }
                else
                {
                    s << "   ";
                }
            }
            for (std::size_t ofs = 0; ofs < 16; ++ofs)
            {
                std::size_t idx = base + ofs;
                if (idx < size && std::isprint(buff[idx]))
                {
                    s << buff[idx];
                }
                else
                {
                    s << " ";
                }
            }
            s << std::endl;
        }
    }

}