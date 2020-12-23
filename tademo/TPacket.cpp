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
        for (std::size_t i = 3u; i <= result.size() - 4; ++i)
        {
            result[i] ^= std::uint8_t(i);
            check += result[i];
        }

        return result;
    }

    bytestring TPacket::encrypt(const bytestring& data)
    {
        bytestring result = data;

        if (data.size() < 4)
        {
            result += std::uint8_t(0x06);
            return result;
        }

        std::uint16_t check = 0u;
        for (std::size_t i = 3u; i <= result.size() - 4; ++i)
        {
            result[i] ^= std::uint8_t(i);
            check += result[i];
        }
        result[1] = check & 0x00ff;
        result[2] = check >> 8;
        return result;
    }


    bytestring TPacket::compress(const bytestring &data)
    {
        int index, cbf, count, a, matchl, cmatchl;
        std::uint16_t kommando, match;
        std::uint16_t *p;

        bytestring result;
        count = 7;
        index = 4;
        while (index < data.size() + 1)
        {
            if (count == 7)
            {
                count = -1;
                result += std::uint8_t(0);
                cbf = result.size();
            }
            ++count;
            if (index < 6 || index>2000)
            {
                result += data[index - 1];
                ++index;
            }
            else
            {
                matchl = 2;
                for (a = 4; a < index - 1; ++a)
                {
                    cmatchl = 0;
                    while (a + cmatchl < index && index + cmatchl < data.size() && data[a + cmatchl - 1] == data[index + cmatchl - 1])
                    {
                        ++cmatchl;
                    }
                    if (cmatchl > matchl)
                    {
                        matchl = cmatchl;
                        match = a;
                        if (matchl > 17)
                        {
                            break;
                        }
                    }
                }
                cmatchl = 0;
                while (index + cmatchl < data.size() && data[index + cmatchl - 1] == data[index - 2])
                {
                    ++cmatchl;
                }
                if (cmatchl > matchl)
                {
                    matchl = cmatchl;
                    match = index - 1;
                }
                if (matchl>2)
                {
                    result[cbf - 1] |= (1 << count);
                    matchl = (matchl - 2) & 0x0f;
                    kommando = ((match - 3) << 4) | matchl;
                    result += bytestring((const std::uint8_t*)"\0\0", 2);
                    p = (std::uint16_t*)&result[result.size() - 2];
                    *p = kommando;
                    index += matchl + 2;
                }
                else
                {
                    result += data[index - 1];
                    ++index;
                }
            }
        }
        if (count == 7)
        {
            result += 0xff;
        }
        else
        {
            result[cbf - 1] |= (0xff << (count + 1));
        }
        result += bytestring((const std::uint8_t*)"\0\0", 2);

        if (result.size() + 3 < data.size())
        {
            result = bytestring(1, 4) + data[1] + data[2] + result;
        }
        else
        {
            result = data;
            result[0] = 3;
        }
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
                if (index >= data.size())
                {
                    result = data.substr(0, 3) + result;
                    return result;
                }
                if (((cbf >> nump) & 1) == 0)
                {
                    result += data[index];
                    ++index;
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
                        return result;
                    }
                    uop = uop & 0x0f;
                    for (unsigned b = a; b < uop + a + 2; ++b)
                    {
                        result += b < result.size() ? result[b-1] : 0;
                    }
                }
            }
        }
        result = data.substr(0, 3) + result;
        result[0] = 0x03;
        return result;
    }


    unsigned TPacket::getExpectedSubPacketSize(const bytestring &s)
    {
        unsigned len = 0u;
        if (s.empty())
        {
            return len;
        }

        SubPacketCode spc = SubPacketCode(s[0]);

        switch (spc)
        {
        case SubPacketCode:: ZERO:                     // decompression artifact??
        {
            std::size_t pos = s.find_first_not_of(std::uint8_t(0));
            len = pos == bytestring::npos ? s.size() : pos;
            break;
        }
        case SubPacketCode::PING: len = 13;   break;  // ping
        case SubPacketCode::PAD_ENCRYPT: len = 1;    break;
        case SubPacketCode::UNK_07: len = 1;    break;
        case SubPacketCode::STATUS: len = 192;  break;  // status
        case SubPacketCode::UNIT_DATA: len = 14;   break;  // unit data
        case SubPacketCode::UNK_17: len = 2;    break;
        case SubPacketCode::SERVER_NUM: len = 2;    break;  // servernumber
        case SubPacketCode::UNK_15: len = 1;    break;
        case SubPacketCode::UNK_08:  len = 1;    break;
        case SubPacketCode::CHAT:  len = 65;   break;
        case SubPacketCode::SELECT_TEAM: len = 6; break;
        case SubPacketCode::UNK_26:  len = 41;   break;  // 0x26
        case SubPacketCode::UNK_22:  len = 6;    break;  // 0x22
        case SubPacketCode::UNK_2A:  len = 2;    break;  // 0x2a
        case SubPacketCode::UNK_1E: len = 2;    break;
        case SubPacketCode::TICK:  len = unsigned(s[1]) + (unsigned(s[2]) << 8);
            break;                      // 0x2c
            //Nya paket

        case SubPacketCode::UNK_09: len = 23;    break;   //Seems to be the package that gives orders for something newly built to be shown immediately. However, shows for the wrong person ..
        case SubPacketCode::UNK_11: len = 4;     break;   //?? crash
        case SubPacketCode::UNK_10: len = 22;    break;   //Gives explosions! However, they appear in the wrong place
        case SubPacketCode::UNK_12: len = 5;     break;   //?? crash
        case SubPacketCode::UNK_0a: len = 7;     break;   //?? crash
        case SubPacketCode::RESOURCES: len = 58;    break;   //resource stats
        case SubPacketCode::SPEED: len = 3;     break;   /// speed / pause /unpause
        case SubPacketCode::SHOT: len = 36;    break;   //Shot. however, the shots remain. and they miss ..
        case SubPacketCode::SHOT_RESIDUE: len = 9;     break;   //Eliminates shot residues
        case SubPacketCode::POSE: len = 6;     break;   //Makes the commander's upper body rotate correctly when he builds, among other things
        case SubPacketCode::UNIT_DIED: len = 11;    break;   //hmm. seems to give explosions with

        case SubPacketCode::UNK_1F: len = 5;     break;
        case SubPacketCode::UNK_23: len = 14;    break;
        case SubPacketCode::SHARE: len = 17;    break;   // share resources
        case SubPacketCode::REJECT: len = 6;     break;
        case SubPacketCode::UNK_29: len = 3;     break;
        case SubPacketCode::UNK_14: len = 24;    break;
        case SubPacketCode::UNK_21: len = 10;    break;
        case SubPacketCode::UNK_03: len = 7;     break;
        case SubPacketCode::UNK_0e: len = 14;    break;

        case SubPacketCode::SMARTPAK_TICK_FF10: len = 1;     break;     //smartpak packages should not be in the wild
        case SubPacketCode::SMARTPACK_TICK_START: len = 5;     break;     //smartpak
        case SubPacketCode::SMARTPAK_TICK_OTHER: len = unsigned(s[1]) + (unsigned(s[2]) << 8) - 4; //smartpak
            break;

        case SubPacketCode::CHAT_ENEMY: len = 73;    break;     //enemy-chat
        case SubPacketCode::RECORDER_DATA_CONNECT: len = unsigned(s[1]) + 3; //recorder data connect
            break;

        case SubPacketCode::MAP_POSITION: len = 5;     break;     //map position
        case SubPacketCode::UNK_FA: len = 1;     break;
        case SubPacketCode::UNK_F6: len = 1;     break;
        default: len = 0;
        };

        return len;
    }


    // s modified in-place
    bytestring TPacket::split2(bytestring &s, bool smartpak, bool &error)
    {
        error = false;
        if (s.empty())
        {
            return s;
        }

        unsigned len = getExpectedSubPacketSize(s);

        if ((s[0] == 0xff || s[0] == 0xfe || s[0] == 0xfd) && !smartpak)
        {
            error = true;
        }

        if (s.size() < len)
        {
            len = 0;
            error = true;
        }

        bytestring next;
        if (len == 0)
        {
            next = s;
            s.clear();
            error = true;
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
        if (c[0] == 0x04)
        {
            c = TPacket::decompress(c);
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
            bool error;
            bytestring s = TPacket::split2(c, true, error);

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

    bytestring TPacket::trivialSmartpak(const bytestring& subpacket, std::uint32_t tcpseq)
    {
        bytestring result((std::uint8_t*)"\x03\x00\x00", 3);
        result += bytestring((std::uint8_t*) & tcpseq, 4);
        result += subpacket;
        return result;
    }

    bytestring TPacket::createChatSubpacket(const std::string& message)
    {
        char chatMessage[65];
        chatMessage[0] = std::uint8_t(SubPacketCode::CHAT);
        std::strncpy(&chatMessage[1], message.c_str(), 64);
        chatMessage[64] = 0;
        return bytestring((std::uint8_t*)chatMessage, sizeof(chatMessage));
    }


#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)
#define TESTASSERT(x) if (!(x)) { throw std::runtime_error(AT); }

    void TPacket::test()
    {
        const std::uint8_t _compressed[] = {
            0x04, 0xdd, 0x9a, 0x80, 0xff, 0xff, 0xff, 0xff, 0x1a, 0x04, 0x00, 0x75, 0x00, 0xf0, 0x37, 0x02,
            0x00, 0x00, 0x5c, 0x00, 0x5a, 0x00, 0x1a, 0x01, 0xf8, 0x00, 0xff, 0xb6, 0x00, 0x54, 0x00, 0xb4,
            0x00, 0x14, 0x01, 0x92, 0x00, 0xd2, 0x00, 0x12, 0x01, 0x70, 0x00, 0xff, 0x90, 0x00, 0xb0, 0x00,
            0xd0, 0x00, 0xf0, 0x00, 0x10, 0x01, 0x5e, 0x00, 0x7c, 0x00, 0x7a, 0x00, 0xff, 0x58, 0x00, 0xf8,
            0x00, 0xb6, 0x00, 0x54, 0x00, 0xb4, 0x00, 0x14, 0x01, 0x92, 0x00, 0xd2, 0x00, 0xff, 0x12, 0x01,
            0x70, 0x00, 0x90, 0x00, 0xb0, 0x00, 0xd0, 0x00, 0xf0, 0x00, 0x10, 0x01, 0x5e, 0x00, 0xff, 0x7c,
            0x00, 0x7a, 0x00, 0x58, 0x00, 0xf8, 0x00, 0xb6, 0x00, 0x54, 0x00, 0xb4, 0x00, 0x14, 0x01, 0xff,
            0x92, 0x00, 0xd2, 0x00, 0x12, 0x01, 0x70, 0x00, 0x90, 0x00, 0xb0, 0x00, 0xd0, 0x00, 0xf0, 0x00,
            0xff, 0x10, 0x01, 0x5e, 0x00, 0x7c, 0x00, 0x7a, 0x00, 0x58, 0x00, 0xf8, 0x00, 0xb6, 0x00, 0x54,
            0x00, 0xff, 0xb4, 0x00, 0x14, 0x01, 0x92, 0x00, 0xd2, 0x00, 0x12, 0x01, 0x70, 0x00, 0x90, 0x00,
            0xb0, 0x00, 0xff, 0xd0, 0x00, 0xf0, 0x00, 0x10, 0x01, 0x5e, 0x00, 0x7c, 0x00, 0x7a, 0x00, 0x58,
            0x00, 0xf8, 0x00, 0xff, 0xb2, 0x00, 0xfe, 0x00, 0x1e, 0x01, 0x5e, 0x00, 0x7e, 0x00, 0x9e, 0x00,
            0xbe, 0x00, 0xde, 0x00, 0xff, 0xfe, 0x00, 0x1e, 0x01, 0x5e, 0x00, 0x7e, 0x00, 0x9e, 0x00, 0xbe,
            0x00, 0xde, 0x00, 0xfe, 0x00, 0xff, 0x1e, 0x01, 0x5e, 0x00, 0x7e, 0x00, 0x9e, 0x00, 0xbe, 0x00,
            0xde, 0x00, 0xfe, 0x00, 0x1e, 0x01, 0x7f, 0x5e, 0x00, 0x7e, 0x00, 0x9e, 0x00, 0xbe, 0x00, 0xde,
            0x00, 0xfe, 0x00, 0x1e, 0x01, 0x1b, 0x00, 0x7b, 0xc6, 0xc5, 0x16, 0x01, 0x20, 0x43, 0x61, 0x00,
            0x6e, 0x61, 0x6c, 0x20, 0x43, 0x72, 0x6f, 0x73, 0xf0, 0x73, 0x69, 0x6e, 0x67, 0x76, 0x00, 0x33,
            0x40, 0x8e, 0x40, 0x8e, 0x41, 0x1f, 0x8e, 0x42, 0x8e, 0x43, 0xce, 0x3f, 0xce, 0x3f, 0xce, 0x3f,
            0x80, 0x07, 0x38, 0x48, 0x04, 0x00, 0x72, 0x92, 0x3e, 0x00, 0x01, 0x71, 0x00, 0x08, 0x04, 0x08,
            0x00, 0x62, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0xdc, 0x80, 0x05, 0x03, 0x01, 0x29, 0x05, 0xbe, 0x6e,
            0x75, 0x00, 0xfa, 0x06, 0x71, 0x00, 0x00, 0x00, 0x00
        };

        bytestring compressed(_compressed, sizeof(_compressed));
        std::cout << "compressed:\n";
        HexDump(compressed.data(), compressed.size(), std::cout);

        bytestring decompressed = decompress(compressed);
        std::cout << "decompressed:\n";
        HexDump(decompressed.data(), decompressed.size(), std::cout);

        std::vector<bytestring> subpackets = unsmartpak(decompressed, 3);
        for (const bytestring& s : subpackets)
        {
            unsigned expectedSize = getExpectedSubPacketSize(s);
            TESTASSERT(expectedSize != 0);
            TESTASSERT(expectedSize == s.size());
        }
    }

}
