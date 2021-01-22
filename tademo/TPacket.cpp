#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cctype>
#include <sstream>
#include <vector>
#include <map>

#include "TPacket.h"
#include "HexDump.h"
#include "TestPackets.h"

namespace TADemo
{

    void TPacket::decrypt(bytestring &data, std::size_t ofs, ::uint16_t &checkExtracted, std::uint16_t &checkCalculated)
    {
        if (data.size() < 4+ofs)
        {
            data += std::uint8_t(0x06);
            return;
        }

        checkExtracted = *(std::uint16_t*)&data[ofs+1];
        checkCalculated = 0u;
        std::uint8_t xorKey = 3u;
        std::size_t i = ofs+3u;
        for (; i <= data.size() - 4; ++i)
        {
            checkCalculated += data[i];
            data[i] ^= xorKey;
            ++xorKey;
        }
    }

    void TPacket::encrypt(bytestring& data)
    {
        if (data.size() < 4)
        {
            data += std::uint8_t(0x06);
            return;
        }

        std::uint16_t check = 0u;
        for (std::size_t i = 3u; i <= data.size() - 4; ++i)
        {
            data[i] ^= std::uint8_t(i);
            check += data[i];
        }
        data[1] = check & 0x00ff;
        data[2] = check >> 8;
    }


    bytestring TPacket::compress(const bytestring &data)
    {
        unsigned index, cbf, count, a, matchl, cmatchl;
        std::uint16_t kommando, match;
        std::uint16_t *p;

        bytestring result;
        count = 7u;
        index = 4u;
        while (index < data.size() + 1u)
        {
            if (count == 7u)
            {
                count = 0u;
                result += std::uint8_t(0u);
                cbf = result.size();
            }
            else
            {
                ++count;
            }
            if (index < 6u || index>2000u)
            {
                result += data[index - 1u];
                ++index;
            }
            else
            {
                matchl = 2u;
                for (a = 4u; a < index - 1u; ++a)
                {
                    cmatchl = 0;
                    while (a + cmatchl < index && index + cmatchl < data.size() && data[a + cmatchl - 1u] == data[index + cmatchl - 1u])
                    {
                        ++cmatchl;
                    }
                    if (cmatchl > matchl)
                    {
                        matchl = cmatchl;
                        match = a;
                        if (matchl > 17u)
                        {
                            break;
                        }
                    }
                }
                cmatchl = 0u;
                while (index + cmatchl < data.size() && data[index + cmatchl - 1u] == data[index - 2u])
                {
                    ++cmatchl;
                }
                if (cmatchl > matchl)
                {
                    matchl = cmatchl;
                    match = index - 1u;
                }
                if (matchl>2)
                {
                    result[cbf - 1u] |= (1u << count);
                    matchl = (matchl - 2u) & 0x0f;
                    kommando = ((match - 3u) << 4) | matchl;
                    result += bytestring((const std::uint8_t*)"\0\0", 2);
                    p = (std::uint16_t*)&result[result.size() - 2u];
                    *p = kommando;
                    index += matchl + 2u;
                }
                else
                {
                    result += data[index - 1u];
                    ++index;
                }
            }
        }
        if (count == 7u)
        {
            result += 0xff;
        }
        else
        {
            result[cbf - 1u] |= (0xff << (count + 1u));
        }
        result += bytestring((const std::uint8_t*)"\0\0", 2u);

        if (result.size() + 3u < data.size())
        {
            result = bytestring(1u, 0x04) + data[1u] + data[2u] + result;
        }
        else
        {
            result = data;
            result[0] = 0x03;
        }
        return result;
    }

    bytestring TPacket::decompress(const bytestring &data, const unsigned headerSize)
    {
        return decompress(data.data(), data.size(), headerSize);
    }

    bytestring TPacket::decompress(const std::uint8_t *data, const unsigned len, const unsigned headerSize)
    {
        bytestring result;
        if (data[0] != 0x04)
        {
            result.append(data, len);
            return result;
        }

        result.reserve(std::max(0x1000u, 2*len));
        result.append(data, headerSize);
        result[0] = 0x03;

        unsigned index = headerSize;
        while (index < len)
        {
            unsigned cbf = data[index];
            ++index;

            for (unsigned nump = 0; nump < 8; ++nump)
            {
                if (index >= len)
                {
                    // error: ran out of bytes. if you get here theres a bug
                    result[0] = 0x04;
                    return result;
                }
                if (((cbf >> nump) & 1) == 0)
                {
                    result += data[index];
                    ++index;
                }
                else
                {
                    unsigned uop = *(std::uint16_t*)(&data[index]);
                    index += 2;
                    unsigned a = uop >> 4;
                    if (a == 0)
                    {
                        return result;
                    }
                    uop = uop & 0x0f;
                    a += headerSize;
                    for (unsigned b = a; b < uop + a + 2; ++b)
                    {
                        result += b <= result.size() ? result[b-1] : 0;
                    }
                }
            }
        }
        return result;
    }

    unsigned TPacket::getExpectedSubPacketSize(const bytestring &bytes)
    {
        return getExpectedSubPacketSize(bytes.data(), bytes.size());
    }

    unsigned TPacket::getExpectedSubPacketSize(const std::uint8_t *s, unsigned sz)
    {
        if (sz == 0u)
        {
            return 0u;
        }

        unsigned len = 0u;
        SubPacketCode spc = SubPacketCode(s[0]);

        switch (spc)
        {
        case SubPacketCode:: ZERO_00:
            for (; len < sz && s[len] == 0u; ++len);
            break;
        case SubPacketCode::PING_02: len = 13;   break;
        case SubPacketCode::UNK_03: len = 7;     break;
        case SubPacketCode::CHAT_05:
            len = 65;
            if (s[len - 1] != 0)
            {
                // older recorder versions sometimes emit more text than they should
                // however, it is send as a single packet.
                len = sz;
                // And if map position is enabled, the last 5 bytes should be the map
                // pos data
                if (SubPacketCode(s[len - 5]) == SubPacketCode::MAP_POSITION_FC)
                {
                    len -= 5;
                }
            }
            break;
        case SubPacketCode::PAD_ENCRYPT_06: len = 1;    break;
        case SubPacketCode::UNK_07: len = 1;    break;
        case SubPacketCode::LOADING_STARTED_08:  len = 1;    break;
        case SubPacketCode::UNIT_BUILD_STARTED_09: len = 23;    break;
        case SubPacketCode::UNK_0A: len = 7;     break;
        case SubPacketCode::UNIT_TAKE_DAMAGE_0B: len = 9;     break;
        case SubPacketCode::UNIT_KILLED_0C: len = 11;    break;
        case SubPacketCode::WEAPON_FIRED_0D: len = 36;    break;
        case SubPacketCode::AREA_OF_EFFECT_0E: len = 14;    break;
        case SubPacketCode::FEATURE_ACTION_0F: len = 6;     break;
        case SubPacketCode::UNIT_START_SCRIPT_10: len = 22;    break;
        case SubPacketCode::UNIT_STATE_11: len = 4;     break;
        case SubPacketCode::UNIT_BUILD_FINISHED_12: len = 5;     break;
        case SubPacketCode::GIVE_UNIT_14: len = 24;    break;
        case SubPacketCode::UNK_15: len = 1;    break;
        case SubPacketCode::SHARE_RESOURCES_16: len = 17;    break;
        case SubPacketCode::UNK_17: len = 2;    break;
        case SubPacketCode::HOST_MIGRATION_18: len = 2;    break;
        case SubPacketCode::SPEED_19: len = 3;     break;
        case SubPacketCode::UNIT_TYPES_SYNC_1A: len = 14;   break;
        case SubPacketCode::REJECT_1B: len = 6;     break;
        case SubPacketCode::UNK_1E: len = 2;    break;
        case SubPacketCode::UNK_1F: len = 5;     break;
        case SubPacketCode::PLAYER_INFO_20: len = 192;  break;
        case SubPacketCode::UNK_21: len = 10;    break;
        case SubPacketCode::UNK_22:  len = 6;    break;
        case SubPacketCode::ALLY_23: len = 14;    break;
        case SubPacketCode::TEAM_24: len = 6; break;
        case SubPacketCode::UNK_26:  len = 41;   break;
        case SubPacketCode::PLAYER_RESOURCE_INFO_28: len = 58;    break;
        case SubPacketCode::UNK_29: len = 3;     break;
        case SubPacketCode::UNK_2A:  len = 2;    break;
        case SubPacketCode::UNIT_STAT_AND_MOVE_2C:
            if (sz >= 3) len = *(std::uint16_t*)(&s[1]);
            break;
        case SubPacketCode::UNK_2E: len = 9; break;
        case SubPacketCode::UNK_F6: len = 1;     break;
        case SubPacketCode::ENEMY_CHAT_F9: len = 73;    break;
        case SubPacketCode::REPLAYER_SERVER_FA: len = 1;     break;
        case SubPacketCode::RECORDER_DATA_CONNECT_FB:
            if (sz >= 2) len = unsigned(s[1]) + 3;
            break;
        case SubPacketCode::MAP_POSITION_FC: len = 5;     break;
        case SubPacketCode::SMARTPAK_TICK_OTHER_FD:
            if (sz >= 3) len = *(std::uint16_t*)(&s[1]) - 4;
            break;
        case SubPacketCode::SMARTPAK_TICK_START_FE: len = 5;     break;
        case SubPacketCode::SMARTPAK_TICK_FF: len = 1;     break;
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
        unsigned i = 0u;  // index into s
        while (start > 7)
        {
            // skip bytes
            ++i;
            start -= 8u;
        };

        int result = 0u;
        std::uint8_t mask = 1 << start;
        std::uint8_t byte = s[i];

        for (unsigned j = 0u; j < num; ++j)
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
                mask = 1u;
            }
        }
        return result;
    }

    std::vector<bytestring> TPacket::slowUnsmartpak(const bytestring &_c, bool hasTimestamp, bool hasChecksum)
    {
        bytestring c;

        if (hasChecksum)
        {
            c = _c;
        }
        else
        {
            c = _c.substr(0, 1) + (const std::uint8_t *)"xx" + _c.substr(1);
        }

        if (c[0] == 0x04)
        {
            c = TPacket::decompress(c, 3);
        }

        if (hasTimestamp)
        {
            c = c.substr(7);
        }
        else
        {
            c = c.substr(3);
        }

        std::vector<bytestring> ut;
        std::uint32_t packnum = 0u;
        while (!c.empty())
        {
            bool error;
            bytestring s = TPacket::split2(c, true, error);

            switch (SubPacketCode(s[0]))
            {
            case SubPacketCode::SMARTPAK_TICK_START_FE:
            {
                packnum = *(std::uint32_t*)(&s[1]);
                break;
            }

            case SubPacketCode::SMARTPAK_TICK_FF:
            {
                bytestring tmp({ ',', 0x0b, 0, 'x', 'x', 'x', 'x', 0xff, 0xff, 1, 0 });
                *(std::uint32_t*)(&tmp[3]) = packnum;
                ++packnum;
                ut.push_back(tmp);
                break;
            }

            case SubPacketCode::SMARTPAK_TICK_OTHER_FD:
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

    std::vector<bytestring> TPacket::unsmartpak(const bytestring &_c, bool hasTimestamp, bool hasChecksum)
    {
        const std::uint8_t *ptr = _c.data();;
        const std::uint8_t *end = ptr + _c.size();

        bytestring buffer;
        if (_c[0] == 0x04)
        {
            buffer = decompress(_c, hasChecksum ? 3 : 1);
            ptr = buffer.data();
            end = ptr + buffer.size();
        }

        ++ptr;
        if (hasChecksum) ptr += 2;
        if (hasTimestamp) ptr += 4;

        std::vector<bytestring> ut;
        std::uint32_t packnum = 0u;
        while (ptr < end)
        {
            unsigned subpakLen = getExpectedSubPacketSize(ptr, end-ptr);
            if (subpakLen == 0 || ptr+subpakLen > end)
            {
                subpakLen = end - ptr;
            }

            switch (SubPacketCode(ptr[0]))
            {
            case SubPacketCode::SMARTPAK_TICK_START_FE:
            {
                packnum = *(std::uint32_t*)(&ptr[1]);
                break;
            }

            case SubPacketCode::SMARTPAK_TICK_FF:
            {
                bytestring tmp({ ',', 0x0b, 0, 'x', 'x', 'x', 'x', 0xff, 0xff, 1, 0 });
                *(std::uint32_t*)(&tmp[3]) = packnum;
                ++packnum;
                ut.push_back(tmp);
                break;
            }

            case SubPacketCode::SMARTPAK_TICK_OTHER_FD:
            {
                ut.push_back(bytestring());
                bytestring &tmp = ut.back();
                tmp.reserve(end - ptr + 4);
                tmp.append(ptr, 3);
                tmp.append((std::uint8_t*)&packnum, 4);
                tmp.append(ptr + 3, end);
                ++packnum;
                tmp[0] = 0x2c;
                break;
            }

            default:
                ut.push_back(bytestring());
                ut.back().append(ptr, subpakLen);
            };
            ptr += subpakLen;
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
            if (code == SubPacketCode::UNIT_STAT_AND_MOVE_2C)
            {
                std::uint32_t thisPacknum = *(std::uint32_t*)(&sn[3]);
                if (firstpak || thisPacknum != packnum)
                {
                    firstpak = false;
                    packnum = thisPacknum;
                    tosave += std::uint8_t(SubPacketCode::SMARTPAK_TICK_START_FE);
                    tosave += sn.substr(3, 4);
                }
                bytestring shorterTick = sn.substr(0, 3) + sn.substr(7);
                shorterTick[0] = std::uint8_t(SubPacketCode::SMARTPAK_TICK_OTHER_FD);
                if (shorterTick[1] == 0x0b && shorterTick[2] == 0x00)
                {
                    shorterTick.resize(1);
                    shorterTick[0] = std::uint8_t(SubPacketCode::SMARTPAK_TICK_FF);
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
            std::uint16_t checksum[2];
            bytestring decrypted(encrypted);
            decrypt(decrypted, 0u, checksum[0], checksum[1]);
            bytestring decompressed = decompress(decrypted, 3);
            std::vector<bytestring> unpaked = unsmartpak(decompressed, true, true);
            smartpak(unpaked, maxCompressedSize, results, 0, unpaked.size());

            for (auto &r : results)
            {
                encrypt(r);
            }
        }
        return results;
    }

    bytestring TPacket::createChatSubpacket(const std::string& message)
    {
        char chatMessage[65];
        chatMessage[0] = std::uint8_t(SubPacketCode::CHAT_05);
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
        //testCompression(16);
        //testCompression(32);
        //testCompression(64);
        //return;

        std::map<int, unsigned> badSubPacketCounts;
        std::uint8_t filter = 0x00;

        using namespace TADemo::TestPackets;
        ++badSubPacketCounts[testDecode(td1, true, true, filter)];
        ++badSubPacketCounts[testDecode(td2, true, true, filter)];
        ++badSubPacketCounts[testDecode(td3, true, true, filter)];
        ++badSubPacketCounts[testDecode(td4, true, true, filter)];
        ++badSubPacketCounts[testDecode(td5, true, true, filter)];
        ++badSubPacketCounts[testDecode(td6, true, true, filter)];
        ++badSubPacketCounts[testDecode(td7, true, true, filter)];
        ++badSubPacketCounts[testDecode(td8, true, true, filter)];
        ++badSubPacketCounts[testDecode(td9, true, true, filter)];
        ++badSubPacketCounts[testDecode(td10, true, true, filter)];
        ++badSubPacketCounts[testDecode(td11, true, true, filter)];
        ++badSubPacketCounts[testDecode(td12, true, true, filter)];
        ++badSubPacketCounts[testDecode(td13, true, true, filter)];
        ++badSubPacketCounts[testDecode(td14, true, true, filter)];
        ++badSubPacketCounts[testDecode(td15, true, true, filter)];
        ++badSubPacketCounts[testDecode(td16, true, true, filter)];
        ++badSubPacketCounts[testDecode(td17, true, true, filter)];
        ++badSubPacketCounts[testDecode(td18, true, true, filter)];
        ++badSubPacketCounts[testDecode(td19, true, true, filter)];
        ++badSubPacketCounts[testDecode(td20, true, true, filter)];
        ++badSubPacketCounts[testDecode(td21, true, true, filter)];
        ++badSubPacketCounts[testDecode(td22, true, true, filter)];
        ++badSubPacketCounts[testDecode(td23, true, true, filter)];
        ++badSubPacketCounts[testDecode(td24, true, true, filter)];
        ++badSubPacketCounts[testDecode(td25, true, true, filter)];
        ++badSubPacketCounts[testDecode(td26, true, true, filter)];
        ++badSubPacketCounts[testDecode(td27, true, true, filter)];
        ++badSubPacketCounts[testDecode(td28, true, true, filter)];
        ++badSubPacketCounts[testDecode(td29, true, true, filter)];
        ++badSubPacketCounts[testDecode(td30, true, true, filter)];
        ++badSubPacketCounts[testDecode(td31, true, true, filter)];
        ++badSubPacketCounts[testDecode(td32, true, true, filter)];
        ++badSubPacketCounts[testDecode(td33, true, true, filter)];
        ++badSubPacketCounts[testDecode(td34, true, true, filter)];
        ++badSubPacketCounts[testDecode(td35, true, true, filter)];
        ++badSubPacketCounts[testDecode(td36, true, true, filter)];
        ++badSubPacketCounts[testDecode(td37, true, true, filter)];
        ++badSubPacketCounts[testDecode(td38, true, true, filter)];

        for (auto p : badSubPacketCounts)
        {
            std::cout << "bad subpak code:" << std::hex << p.first << ", count=" << p.second << '\n';
        }
    }

    bool TPacket::testUnpakability(bytestring s, int recurseDepth)
    {
        unsigned sz = TPacket::getExpectedSubPacketSize(s);
        if (sz == 0 || sz > s.size() || s[0] == 0)
        {
            return false;
        }
        else if (recurseDepth>0)
        {
            return testUnpakability(s.substr(sz), recurseDepth-1);
        }
        else
        {
            return true;
        }
    }

    void TPacket::testCompression(unsigned dictionarySize)
    {
        std::vector<bytestring> dictionary(dictionarySize);
        for (bytestring & symbol : dictionary)
        {
            unsigned N = 1 + std::rand() % 31;  // random sized symbols up to 32 bytes in length
            for (unsigned n = 0; n < N; ++n)
            {
                symbol.append(1, std::rand() % 0xff);
            }
        }

        const unsigned I = 10000u;
        unsigned nPass = 0;
        unsigned nFail = 0;
        for (unsigned i = 0u; i < I; ++i)
        {
            const unsigned N = std::rand() % 256;   // select upto 255 symbols from the dictionary
            bytestring data;
            data.reserve(N + 3);
            data.append((const std::uint8_t*)"\x03\x00\x00", 3);
            for (unsigned n = 0; n < N; ++n)
            {
                data.append(dictionary[std::rand() % dictionarySize]);  // append a random word
            }
            bytestring compressed = compress(data);
            bytestring decompressed = decompress(compressed, 3);

            if (decompressed == data)
            {
                ++nPass;
            }
            else
            {
                ++nFail;
                std::cout << "---------- compress/decompress error\n";
                std::cout << "original:\n";
                HexDump(data.data(), data.size(), std::cout);
                std::cout << "compressed:\n";
                HexDump(compressed.data(), compressed.size(), std::cout);
                std::cout << "decompr:\n";
                HexDump(decompressed.data(), decompressed.size(), std::cout);
            }

            if (i % 100 == 0)
            {
                std::cout << "nPass:" << nPass << ", nFail:" << nFail << '\r';
            }
        }
        std::cout << std::endl;
    }

    int TPacket::testDecode(const bytestring & encrypted, bool hasTimestamp, bool hasChecksum, std::uint8_t filter)
    {
        std::cout << "-------------------- encrypted len=" << std::dec << encrypted.size() << "\n";

        std::uint16_t checksum[2];
        bytestring decrypted(encrypted);
        decrypt(decrypted, 0u, checksum[0], checksum[1]);
        //std::cout << "decrypted: (checkpacked=" << checksum[0] << ", checkcalced=" << checksum[1] << ")\n";
        //HexDump(decrypted.data(), decrypted.size(), std::cout);

        bytestring decompressed = decompress(decrypted, 3);
        //std::cout << "decompressed:\n";
        //HexDump(decompressed.data(), decompressed.size(), std::cout);

        std::vector<bytestring> unpaked = unsmartpak(decompressed, hasTimestamp, hasChecksum);
        int nSubPak = 0;
        for (const auto &subpak : unpaked)
        {
            ++nSubPak;
            unsigned sizeExpected = TPacket::getExpectedSubPacketSize(subpak);

            if (sizeExpected == subpak.size())
            {
                if (filter == 0u || subpak[0] == filter)
                {
                    std::cout << "subpacket " << nSubPak << " of " << unpaked.size() << ": (szExpected=" << std::hex << sizeExpected << ", sizeActual=" << subpak.size() << ")\n";
                    HexDump(subpak.data(), subpak.size(), std::cout);
                }
            }
            else
            {
                if (filter == 0u || subpak[0] == filter)
                {
                    std::cout << "***UNKNOWN SUBPACKET: (szExpected=" << std::hex << sizeExpected << ", sizeActual=" << subpak.size() << ")\n";
                    HexDump(subpak.data(), subpak.size(), std::cout);
                    for (std::size_t ofs = 1; ofs < subpak.size(); ++ofs)
                    {
                        bytestring testpak = subpak.substr(ofs);
                        if (testUnpakability(testpak, 5))
                        {
                            std::cout << "possible futher subpackets at ofs=" << std::hex << ofs << '\n';
                            while (!testpak.empty())
                            {
                                bool error;
                                bytestring subtestpak = TPacket::split2(testpak, true, error);
                                HexDump(subtestpak.data(), subtestpak.size(), std::cout);
                            }
                            break;
                        }
                    }
                }
                return subpak[0];
            }
        }
        return -1;
    }
}
