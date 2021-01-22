#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace TADemo
{

    typedef std::basic_string<std::uint8_t> bytestring;

    enum class PacketCode
    {
        UNCOMPRESSED = 0x03,
        COMPRESSED = 0x04
    };

    enum class SubPacketCode
    {
        ZERO_00 = 0x00,
        PING_02 = 0x02,
        UNK_03 = 0x03,
        CHAT_05 = 0x05,
        PAD_ENCRYPT_06 = 0x06,
        UNK_07 = 0x07,
        LOADING_STARTED_08 = 0x08,
        UNIT_BUILD_STARTED_09 = 0x09,
        UNK_0A = 0x0a,
        UNIT_TAKE_DAMAGE_0B = 0x0b,
        UNIT_KILLED_0C = 0x0c,
        WEAPON_FIRED_0D = 0x0d,
        AREA_OF_EFFECT_0E = 0x0e,
        FEATURE_ACTION_0F = 0x0f,
        UNIT_START_SCRIPT_10 = 0x10,
        UNIT_STATE_11 = 0x11,
        UNIT_BUILD_FINISHED_12 = 0x12,
        PLAY_SOUND_13 = 0x13,
        GIVE_UNIT_14 = 0x14,
        UNK_15 = 0x15,
        SHARE_RESOURCES_16 = 0x16,
        UNK_17 = 0x17,
        HOST_MIGRATION_18 = 0x18,
        SPEED_19 = 0x19,
        UNIT_TYPES_SYNC_1A = 0x1a,
        REJECT_1B = 0x1b,
        UNK_1E = 0x1e,
        UNK_1F = 0x1f,
        PLAYER_INFO_20 = 0x20,
        UNK_21 = 0x21,
        UNK_22 = 0x22,
        ALLY_23 = 0x23,
        TEAM_24 = 0x24,
        UNK_26  = 0x26,
        PLAYER_RESOURCE_INFO_28 = 0x28,
        UNK_29  = 0x29,
        UNK_2A  = 0x2a,
        UNIT_STAT_AND_MOVE_2C = 0x2c,
        UNK_2E = 0x2e,
        UNK_F6  = 0xf6,
        ENEMY_CHAT_F9 = 0xf9,
        REPLAYER_SERVER_FA = 0xfa,
        RECORDER_DATA_CONNECT_FB   = 0xfb,
        MAP_POSITION_FC    = 0xfc,
        SMARTPAK_TICK_OTHER_FD = 0xfd,
        SMARTPAK_TICK_START_FE= 0xfe,
        SMARTPAK_TICK_FF  = 0xff
    };

    class TPacket
    {
    public:
        static unsigned getExpectedSubPacketSize(const bytestring &bytes);
        static unsigned getExpectedSubPacketSize(const std::uint8_t *s, unsigned sz);
        static void decrypt(bytestring& data, std::size_t ofs, std::uint16_t &checkExtracted, std::uint16_t &checkCalculated);
        static void encrypt(bytestring &data);
        static bytestring compress(const bytestring &data);
        static bytestring decompress(const bytestring &data, const unsigned headerSize);
        static bytestring decompress(const std::uint8_t *data, const unsigned len, const unsigned headerSize);
        static bytestring split2(bytestring &s, bool smartpak, bool &error);

        static std::vector<bytestring> slowUnsmartpak(const bytestring &c, bool hasTimestamp, bool hasChecksum);
        static std::vector<bytestring> unsmartpak(const bytestring &c, bool hasTimestamp, bool hasChecksum);
        static bytestring trivialSmartpak(const bytestring& subpacket, std::uint32_t tcpseq);
        static bytestring smartpak(const std::vector<bytestring> &subpackets, std::size_t from, std::size_t to);
        static void smartpak(const std::vector<bytestring> &unpaked, std::size_t maxCompressedSize, std::vector<bytestring> &resultsPakedAndCompressed, std::size_t from, std::size_t to);
        static std::vector<bytestring> resmartpak(const bytestring &encrypted, std::size_t maxCompressedSize);
        static bytestring createChatSubpacket(const std::string& message);

        // @param s string who's characters contain bits of an integer <char1>,<char2>,<char3>,<char4>,etc
        // @param start number of bits into the string from where the number starts.  eg 0 starts at LSB of char1.  8 starts at LSB of char2.
        // @param num number of bits to extract
        static unsigned bin2int(const bytestring &s, unsigned start, unsigned num);

        static void test();
        static int testDecode(const bytestring &encrypted, bool hasTimestamp, bool hasChecksum, std::uint8_t filter);
        static void testCompression(unsigned dictionarySize);
        static bool testUnpakability(bytestring s, int recurseDepth);
    };


    struct DPAddress;
    class TaPacketHandler
    {
    public:
        virtual void onDplaySuperEnumPlayerReply(std::uint32_t dplayId, const std::string &playerName, DPAddress *tcp, DPAddress *udp) = 0;
        virtual void onDplayCreateOrForwardPlayer(std::uint16_t command, std::uint32_t dplayId, const std::string &name, DPAddress *tcp, DPAddress *udp) = 0;
        virtual void onDplayDeletePlayer(std::uint32_t dplayId) = 0;

        virtual void onStatus(
            std::uint32_t sourceDplayId, const std::string &mapName, std::uint16_t maxUnits, 
            unsigned playerSlotNumber, int playerSide, bool isWatcher, bool isAI, bool cheats) = 0;
        virtual void onChat(std::uint32_t sourceDplayId, const std::string &chat) = 0;
        virtual void onUnitDied(std::uint32_t sourceDplayId, std::uint16_t unitId) = 0;
        virtual void onRejectOther(std::uint32_t sourceDplayId, std::uint32_t rejectedDplayId) = 0;
        virtual void onGameTick(std::uint32_t sourceDplayId, std::uint32_t tick) = 0;
    };

}
