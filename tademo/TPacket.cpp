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

            switch (SubPacketCode(s[0]))
            {
            case SubPacketCode::SMARTPACK_TICK_START:
            {
                packnum = *(std::uint32_t*)(&s[1]);
                break;
            }

            case SubPacketCode::SMARTPAK_TICK_FF10:
            {
                bytestring tmp({ ',', 0x0b, 0, 'x', 'x', 'x', 'x', 0xff, 0xff, 1, 0 });
                *(std::uint32_t*)(&tmp[3]) = packnum;
                ++packnum;
                ut.push_back(tmp);
                break;
            }

            case SubPacketCode::SMARTPAK_TICK_OTHER:
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

    bytestring TPacket::smartpak(const std::vector<bytestring> &subpackets, std::size_t from, std::size_t to)
    {
        throw std::runtime_error("smartpak not working. please fix");
        bytestring tosave;
        bool firstpak = true;
        std::uint32_t packnum = 0u;
        for (std::size_t n=from; n<to; ++n)
        {
            const bytestring &sn = subpackets[n];
            SubPacketCode code = SubPacketCode(sn[0]);
            if (code == SubPacketCode::TICK)
            {
                std::uint32_t thisPacknum = *(std::uint32_t*)(&sn[3]);
                if (firstpak || thisPacknum != packnum)
                {
                    firstpak = false;
                    packnum = thisPacknum;
                    tosave += std::uint8_t(SubPacketCode::SMARTPACK_TICK_START);
                    tosave += sn.substr(3, 4);
                }
                bytestring shorterTick = sn.substr(0, 3) + sn.substr(7);
                shorterTick[0] = std::uint8_t(SubPacketCode::SMARTPAK_TICK_OTHER);
                if (shorterTick[1] == 0x0b && shorterTick[2] == 0x00)
                {
                    shorterTick.resize(1);
                    shorterTick[0] = std::uint8_t(SubPacketCode::SMARTPAK_TICK_FF10);
                }
                tosave += shorterTick;
                ++packnum;
            }
            else
            {
                tosave += sn;
            }
        }
        return tosave;
    }

    void TPacket::smartpak(const std::vector<bytestring> &unpaked, std::size_t maxCompressedSize, std::vector<bytestring> &resultsPakedAndCompressed, std::size_t from, std::size_t to)
    {
        std::vector<bytestring> result;

        if (to - from == 0)
        {
            return;
        }
        else if (to - from == 1)
        {
            result.push_back(compress(smartpak(unpaked, from, to)));
            return;
        }
        else
        {
            std::size_t partition = from + (to - from) / 2;
            bytestring left = compress(smartpak(unpaked, from, partition));
            if (left.size() > maxCompressedSize)
            {
                smartpak(unpaked, maxCompressedSize, resultsPakedAndCompressed, from, partition);
            }
            else
            {
                resultsPakedAndCompressed.push_back(left);
            }

            bytestring right = compress(smartpak(unpaked, partition, to));
            if (right.size() > maxCompressedSize)
            {
                smartpak(unpaked, maxCompressedSize, resultsPakedAndCompressed, partition, to);
            }
            else
            {
                resultsPakedAndCompressed.push_back(right);
            }
        }
    }

    std::vector<bytestring> TPacket::resmartpak(const bytestring &encrypted, std::size_t maxCompressedSize)
    {
        std::vector<bytestring> results;
        if (encrypted.size() < maxCompressedSize)
        {
            results.push_back(encrypted);
        }
        else
        {
            bytestring decrypted = decrypt(encrypted);
            bytestring decompressed = decompress(decrypted);
            std::vector<bytestring> unpaked = unsmartpak(decompressed, 3);
            smartpak(unpaked, maxCompressedSize, results, 0, unpaked.size());

            for (auto &r : results)
            {
                r = encrypt(r);
            }

            for (const auto &r : results)
            {
                bytestring rdecomp = decompress(decrypt(r));
            }

        }
        return results;
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
        const std::uint8_t _encrypted[] = {
            0x04, 0xf6, 0x55, 0x3, 0xd0, 0xfb, 0xf9, 0xf8, 0x24, 0x7, 0xa, 0xb9, 0xc, 0x6, 0xe, 0xf,
            0x15, 0x11, 0x73, 0xe3, 0xeb, 0xa1, 0x9, 0x17, 0x49, 0x19, 0xa9, 0x82, 0x1c, 0x6d, 0x1f, 0xab,
            0x51, 0x20, 0x77, 0x13, 0x24, 0x24, 0x24, 0x25, 0x9d, 0x71, 0x28, 0x9d, 0x24, 0x2e, 0x99, 0x3e,
            0x86, 0x32, 0x17, 0x33, 0x8c, 0x56, 0x32, 0x7e, 0xd8, 0x17, 0x3a, 0x3b, 0x38, 0x39, 0xfb, 0x3b,
            0x99, 0x42, 0x42, 0x43, 0x84, 0x4f, 0x46, 0x53, 0x49, 0x15, 0xe9, 0xbd, 0xf, 0x6d, 0x48, 0xea,
            0x56, 0x7d, 0x73, 0x53, 0xed, 0x6, 0x54, 0xda, 0xf8, 0x51, 0x70, 0x5b, 0x78, 0x59, 0x2e, 0x58,
            0x82, 0x92, 0x67, 0x2, 0x64, 0x62, 0x30, 0x24, 0x1c, 0x79, 0x89, 0x6d, 0x8c, 0x69, 0xd4, 0x66,
            0x13, 0x76, 0xb1, 0x9b, 0x84, 0x70, 0x76, 0x93, 0x77, 0x9f, 0x78, 0x98, 0x8e, 0x78, 0x1e, 0x65,
            0x98, 0xa3, 0x41, 0x15, 0x92, 0x9e, 0x21, 0x81, 0xc8, 0x80, 0x31, 0xf2, 0x85, 0x2e, 0x88, 0x6b,
            0x91, 0xc3, 0x98, 0xb3, 0x5b, 0x94, 0xb0, 0x34, 0x72, 0x8b, 0x1f, 0x92, 0x97, 0x21, 0xe6, 0x96,
            0x84, 0x70, 0xa3, 0x43, 0xe6, 0xad, 0x66, 0x27, 0xed, 0x4d, 0x8b, 0x28, 0x57, 0x5d, 0xaf, 0xc7,
            0xbd, 0xd3, 0xf, 0xab, 0xba, 0xd1, 0x5d, 0xbe, 0x99, 0xb5, 0x7a, 0xb7, 0xfc, 0x7d, 0x85, 0xbd,
            0xe2, 0x22, 0x85, 0xc5, 0x62, 0xc3, 0x0, 0x0, 0x0 };

        bytestring encrypted(_encrypted, sizeof(_encrypted));
        std::cout << "encrypted:\n";
        HexDump(encrypted.data(), encrypted.size(), std::cout);

        bytestring decrypted = decrypt(encrypted);
        std::cout << "decrypted:\n";
        HexDump(decrypted.data(), decrypted.size(), std::cout);

        bytestring decompressed = decompress(decrypted);
        std::cout << "decompressed:\n";
        HexDump(decompressed.data(), decompressed.size(), std::cout);

        std::vector<bytestring> unpaked = unsmartpak(decompressed, 3);
        for (const auto &subpak : unpaked)
        {
            std::cout << "subpacket:\n";
            HexDump(decompressed.data(), decompressed.size(), std::cout);
        }

        bytestring repaked = smartpak(unpaked, 0, repaked.size());
        std::cout << "repaked:\n";
        HexDump(repaked.data(), repaked.size(), std::cout);
        TESTASSERT(repaked == decompressed);

        bytestring recompressed = compress(repaked);
        std::cout << "recompressed:\n";
        HexDump(recompressed.data(), recompressed.size(), std::cout);
        TESTASSERT(recompressed == decrypted);

        bytestring reencrypted = encrypt(recompressed);
        std::cout << "reencrypted:\n";
        HexDump(reencrypted.data(), reencrypted.size(), std::cout);
        TESTASSERT(reencrypted == encrypted);

        std::vector<bytestring> parts = resmartpak(encrypted, 200);
        std::vector<bytestring> repakedsubpackets;
        for (const auto &part : parts)
        {
            std::cout << "repaked part (decompressed):\n";
            bytestring temp = decompress(decrypt(part));
            HexDump(temp.data(), temp.size(), std::cout);

            std::cout << "repaked part (subpackets):\n";
            for (const auto &subpacket : unsmartpak(part, 3))
            {
                HexDump(subpacket.data(), subpacket.size(), std::cout);
                repakedsubpackets.push_back(subpacket);
            }
        }
        TESTASSERT(unpaked.size() == repakedsubpackets.size());
        for (std::size_t n = 0u; n < unpaked.size(); ++n)
        {
            TESTASSERT(unpaked[n] == repakedsubpackets[n])
        }
    }
}
