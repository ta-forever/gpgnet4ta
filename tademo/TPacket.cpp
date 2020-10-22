#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cctype>
#include <sstream>
#include <vector>

#include "TPacket.h"
#include "HexDump.h"

namespace TADemo
{

    bytestring TPacket::decrypt(const bytestring &data)
    {
        bytestring result = data;

        if (data.size() < 4)
        {
            result += std::uint8_t(0x06);
            return result;
        }

        std::uint16_t check = 0u;
        for (std::size_t i = 3u; i < result.size() - 4; ++i)
        {
            check += result[i];
            result[i] ^= std::uint8_t(i);
        }

        //const PacketHeader *hdr = (const PacketHeader *)(&data[0]);
        //if (hdr->check != check)
        //{
        //    std::cerr << "TPacket::decrypt checksum failure" << std::endl;
        //}

        return result;
    }

    bytestring TPacket::decompress(const bytestring &data)
    {
        bytestring result;
        if (data[0] != 0x04)
        {
            result = data;
            return result;
        }

        unsigned index = 3;
        while (index < data.size())
        {
            unsigned cbf = data[index];
            ++index;

            for (unsigned nump = 0; nump < 8; ++nump)
            {
                //std::cout << "1+index=" << std::dec << 1+index << ", nump=" << nump << ", len(inbuf)=" << data.size() << ". ";
                if (index >= data.size())
                {
                    result = data.substr(0, 3) + result;
                    //std::cout << "index>length" << std::endl;
                    //HexDump(result.data(), result.size(), std::cout);
                    return result;
                }
                if (((cbf >> nump) & 1) == 0)
                {
                    result += data[index];
                    ++index;
                    //std::cout << "cbf>>nump&1==0" << std::endl;
                    //HexDump(result.data(), result.size(), std::cout);
                }
                else
                {
                    unsigned uop = data[index + 1] << 8;
                    uop += data[index];
                    index += 2;
                    unsigned a = uop >> 4;
                    if (a == 0)
                    {
                        result = data.substr(0, 3) + result;
                        result[0] = 0x03;
                        //std::cout << "a==0" << std::endl;
                        //HexDump(result.data(), result.size(), std::cout);
                        return result;
                    }
                    uop = uop & 0x0f;
                    for (unsigned b = a; b < uop + a + 2; ++b)
                    {
                        result += b < result.size() ? result[b-1] : 0;
                        //std::cout << "uop a to b" << std::endl;
                        //HexDump(result.data(), result.size(), std::cout);
                    }
                }
            }
        }
        result = data.substr(0, 3) + result;
        result[0] = 0x03;
        //std::cout << "eof" << std::endl;
        //HexDump(result.data(), result.size(), std::cout);
        return result;
    }

    // s modified in-place
    bytestring TPacket::split2(bytestring &s, bool smartpak)
    {
        if (s.empty())
        {
            return s;
        }

        unsigned len = 0u;
        switch (s[0])
        {
        case 0x000:                     // decompression artifact??
        {
            std::size_t pos = s.find_first_not_of(std::uint8_t(0));
            len = pos == bytestring::npos ? s.size() : pos;
            break;
        }
        case 0x002: len = 13;   break;  // ping
        case 0x006: len = 1;    break;
        case 0x007: len = 1;    break;
        case 0x020: len = 192;  break;  // status
        case 0x01a: len = 14;   break;  // unit data
        case 0x017: len = 2;    break;
        case 0x018: len = 2;    break;  // servernumber
        case 0x015: len = 1;    break;
        case 0x08:  len = 1;    break;
        case 0x05:  len = 65;   break;
        case '&' :  len = 41;   break;  // 0x26
        case '"' :  len = 6;    break;  // 0x22
        case '*' :  len = 2;    break;  // 0x2a
        case 0x01e: len = 2;    break;
        case ',' :  len = unsigned(s[1]) + (unsigned(s[2])<<8);
            break;                      // 0x2c
        //Nya paket

        case 0x009: len = 23;    break;   //Seems to be the package that gives orders for something newly built to be shown immediately. However, shows for the wrong person ..
        case 0x011: len = 4;     break;   //?? crash
        case 0x010: len = 22;    break;   //Gives explosions! However, they appear in the wrong place
        case 0x012: len = 5;     break;   //?? crash
        case 0x00a: len = 7;     break;   //?? crash
        case 0x028: len = 58;    break;   //resource stats
        case 0x019: len = 3;     break;   /// speed / pause /unpause
        case 0x00d: len = 36;    break;   //Shot. however, the shots remain. and they miss ..
        case 0x00b: len = 9;     break;   //Eliminates shot residues
        case 0x00f: len = 6;     break;   //Makes the commander's upper body rotate correctly when he builds, among other things
        case 0x00c: len = 11;    break;   //hmm. seems to give explosions with

        case 0x01f: len = 5;     break;
        case 0x023: len = 14;    break;
        case 0x016: len = 17;    break;   // share resources
        case 0x01b: len = 6;     break;
        case 0x029: len = 3;     break;
        case 0x014: len = 24;    break;
        case 0x021: len = 10;    break;
        case 0x003: len = 7;     break;
        case 0x00e: len = 14;    break;
                    
        case 0x0ff: len = 1;     break;     //smartpak packages should not be in the wild
        case 0x0fe: len = 5;     break;     //smartpak
        case 0x0fd: len = unsigned(s[1]) + (unsigned(s[2])<<8) - 4; //smartpak
            break;

        case 0x0f9: len = 73;    break;     //enemy-chat
        case 0x0fb: len = unsigned(s[1]) + 3; //recorder data connect
            break;

        case 0x0fc: len = 5;     break;     //map position
        case 0x0fa: len = 1;     break;
        case 0x0f6: len = 1;     break;
        default: len = 0;
        };

        if ((s[0] == 0xff || s[0] == 0xfe || s[0] == 0xfd) && !smartpak)
        {
            std::cout << "TPacket::split2: bad packet\n";
            HexDump(&s[0], s.size(), std::cout);
            //throw std::runtime_error(ss.str());
        }
        if (s.size() < len)
        {
            std::cout << "TPacket::split2: subpacket longer than packet. packet size=" << s.size() << ", expected len=" << len << '\n';
            HexDump(&s[0], s.size(), std::cout);
            //throw std::runtime_error(ss.str());
            len = 0;
        }

        bytestring next;
        if (len == 0)
        {
            //TACstd::cout << "TPacket::split2: unknown packet s[0]==" << (unsigned(s[0])&0xff) << '\n';
            next = s;
            s.clear();
        }
        else
        {
            next = s.substr(0, len);
            s = s.substr(len);
        }
        return next;
    }

    unsigned TPacket::bin2int(const bytestring &s, unsigned start, unsigned num)
    {
        int i = 0;  // index into s
        while (start > 7)
        {
            // skip bytes
            ++i;
            start -= 8;
        };

        int result = 0;
        std::uint8_t mask = 1 << start;
        std::uint8_t byte = s[i];

        for (int j = 0; j < num; ++j)
        {
            // for the jth bit of result
            if (byte & mask)
            {
                result |= (1 << j);
            }

            ++start;
            mask <<= 1;
            if (start > 7)
            {
                // next byte
                ++i;
                byte = s[i];
                start = 0;
                mask = 1;
            }
        }
        return result;
    }

    std::vector<bytestring> TPacket::unsmartpak(const bytestring &_c, unsigned version)
    {
        bytestring c = _c.substr(0, 1) + (const std::uint8_t *)"xx" + _c.substr(1);
        //std::cout << "--- unsmartpak: c\n";
        //HexDump(c.data(), c.size(), std::cout);
        if (c[0] == 0x04)
        {
            c = TPacket::decompress(c);
            //std::cout << "--- unsmartpak: decompressed\n";
            //HexDump(c.data(), c.size(), std::cout);
        }
        c = c.substr(3);
        if (version == 3)
        {
            c = c.substr(4);
        }

        std::vector<bytestring> ut;
        std::uint32_t packnum = 0u;
        while (!c.empty())
        {
            bytestring s = TPacket::split2(c, true);
            //std::cout << "--- unsmartpak: split s\n";
            //HexDump(s.data(), s.size(), std::cout);
            switch (s[0])
            {
            case 0x0fe:
            {
                packnum = *(std::uint32_t*)(&s[1]);
                break;
            }

            case 0x0ff:
            {
                bytestring tmp({ ',', 0x0b, 0, 'x', 'x', 'x', 'x', 0xff, 0xff, 1, 0 });
                *(std::uint32_t*)(&tmp[3]) = packnum;
                ++packnum;
                ut.push_back(tmp);
                break;
            }

            case 0x0fd:
            {
                bytestring tmp = s.substr(0, 3) + (const std::uint8_t*)"zzzz" + s.substr(3);
                *(std::uint32_t*)(&tmp[3]) = packnum;
                ++packnum;
                tmp[0] = 0x02c;
                ut.push_back(tmp);
                break;
            }

            default:
                ut.push_back(s);
            };
        }
        return ut;
    }

}
