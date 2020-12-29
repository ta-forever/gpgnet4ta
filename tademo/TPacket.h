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

    enum class Side {
        ARM = 0,
        CORE = 1,
        WATCH = 2,
        UNKNOWN = 3
    };

    enum class SubPacketCode
    {
        ZERO = 0x00,                    // decompression artifact??
        PING = 0x02,
        UNK_03 = 0x03,
        CHAT = 0x05,
        PAD_ENCRYPT = 0x06,             // 1 byte encryption padding
        UNK_07 = 0x07,
        UNK_08 = 0x08,
        UNK_09 = 0x09,                  //Seems to be the package that gives orders for something newly built to be shown immediately. However, shows for the wrong person ..
        UNK_0a = 0x0a,                  //?? crash
        SHOT_RESIDUE = 0x0b,            //Eliminates shot residues
        UNIT_DIED = 0x0c,                  //hmm. seems to give explosions with
        SHOT = 0x0d,                    //Shot. however, the shots remain. and they miss ..
        UNK_0e = 0x0e,
        POSE = 0x0f,                    //Makes the commander's upper body rotate correctly when he builds, among other things
        UNK_10 = 0x10,                  //Gives explosions! However, they appear in the wrong place
        UNK_11 = 0x11,                  //?? crash
        UNK_12 = 0x12,                  //?? crash
        UNK_14 = 0x14,
        UNK_15 = 0x15,
        SHARE = 0x16,                   // share resources
        UNK_17 = 0x17,
        SERVER_NUM = 0x18,
        SPEED = 0x19,                   // speed / pause /unpause
        UNIT_DATA = 0x1a,
        REJECT = 0x1b,
        UNK_1E = 0x1e,
        UNK_1F = 0x1f,
        STATUS = 0x20,
        UNK_21 = 0x21,
        UNK_22 = 0x22,                  // '"'
        UNK_23 = 0x23,
        SELECT_TEAM = 0x24,
        UNK_26  = 0x26,                  // '&'
        RESOURCES   = 0x28,               //resource stats
        UNK_29  = 0x29,
        UNK_2A  = 0x2a,                  // '*'
        TICK    = 0x2c,                    // continuous stream of incrementing subpacket numbers
        UNK_F6  = 0xf6,
        CHAT_ENEMY = 0xf9,
        UNK_FA = 0xfa,
        RECORDER_DATA_CONNECT   = 0xfb,
        MAP_POSITION    = 0xfc,
        SMARTPAK_TICK_OTHER = 0xfd,
        SMARTPACK_TICK_START= 0xfe,
        SMARTPAK_TICK_FF10  = 0xff
    };


    class TPacket
    {
    public:
        static void test();

        static unsigned getExpectedSubPacketSize(const bytestring &bytes);
        static bytestring decrypt(const bytestring& data);
        static bytestring encrypt(const bytestring &data);
        static bytestring compress(const bytestring &data);
        static bytestring decompress(const bytestring &data);
        static bytestring split2(bytestring &s, bool smartpak, bool &error);

        static std::vector<bytestring> unsmartpak(const bytestring &c, unsigned version);
        static bytestring trivialSmartpak(const bytestring& subpacket, std::uint32_t tcpseq);
        static bytestring smartpak(const std::vector<bytestring> &subpackets, std::size_t from, std::size_t to);
        static void smartpak(const std::vector<bytestring> &unpaked, std::size_t maxCompressedSize, std::vector<bytestring> &resultsPakedAndCompressed, std::size_t from, std::size_t to);
        static std::vector<bytestring> resmartpak(const bytestring &encrypted, std::size_t maxCompressedSize);
        static bytestring createChatSubpacket(const std::string& message);



        // @param s string who's characters contain bits of an integer <char1>,<char2>,<char3>,<char4>,etc
        // @param start number of bits into the string from where the number starts.  eg 0 starts at LSB of char1.  8 starts at LSB of char2.
        // @param num number of bits to extract
        static unsigned bin2int(const bytestring &s, unsigned start, unsigned num);
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
            unsigned playerSlotNumber, Side playerSide, bool isAI, bool cheats) = 0;
        virtual void onChat(std::uint32_t sourceDplayId, const std::string &chat) = 0;
        virtual void onUnitDied(std::uint32_t sourceDplayId, std::uint16_t unitId) = 0;
        virtual void onRejectOther(std::uint32_t sourceDplayId, std::uint32_t rejectedDplayId) = 0;
        virtual void onGameTick(std::uint32_t sourceDplayId, std::uint32_t tick) = 0;
    };

}
