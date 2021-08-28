#pragma once

#include <string>
#include <cinttypes>

namespace tapacket
{

    const std::uint16_t* skipCWStr(const std::uint16_t* p);
    std::string getCWStr(const std::uint16_t* p);

    enum class DPlayCommandCode
    {
        NONE = 0x0000,
        ENUMSESSIONSREPLY = 0x0001,
        ENUMSESSIONS = 0x0002,
        ENUMPLAYERSREPLY = 0x0003,
        ENUMPLAYER = 0x0004,
        REQUESTPLAYERID = 0x0005,
        REQUESTGROUPID = 0x0006,
        REQUESTPLAYERREPLY = 0x0007,
        CREATEPLAYER = 0x0008,
        CREATEGROUP = 0x0009,
        PLAYERMESSAGE = 0x000A,
        DELETEPLAYER = 0x000B,
        DELETEGROUP = 0x000C,
        ADDPLAYERTOGROUP = 0x000D,
        DELETEPLAYERFROMGROUP = 0x000E,
        PLAYERDATACHANGED = 0x000F,
        PLAYERNAMECHANGED = 0x0010,
        GROUPDATACHANGED = 0x0011,
        GROUPNAMECHANGED = 0x0012,
        ADDFORWARDREQUEST = 0x0013,
        PACKET = 0x0015,
        PING = 0x0016,
        PINGREPLY = 0x0017,
        YOUAREDEAD = 0x0018,
        PLAYERWRAPPER = 0x0019,
        SESSIONDESCCHANGED = 0x001A,
        CHALLENGE = 0x001C,
        ACCESSGRANTED = 0x001D,
        LOGONDENIED = 0x001E,
        AUTHERROR = 0x001F,
        NEGOTIATE = 0x0020,
        CHALLENGERESPONSE = 0x0021,
        SIGNED = 0x0022,
        ADDFORWARDREPLY = 0x0024,
        ASK4MULTICAST = 0x0025,
        ASK4MULTICASTGUARANTEED = 0x0026,
        ADDSHORTCUTTOGROUP = 0x0027,
        DELETEGROUPFROMGROUP = 0x0028,
        SUPERENUMPLAYERSREPLY = 0x0029,
        KEYEXCHANGE = 0x002B,
        KEYEXCHANGEREPLY = 0x002C,
        CHAT = 0x002D,
        ADDFORWARD = 0x002E,
        ADDFORWARDACK = 0x002F,
        PACKET2_DATA = 0x0030,
        PACKET2_ACK = 0x0031,
        IAMNAMESERVER = 0x0035,
        VOICE = 0x0036,
        MULTICASTDELIVERY = 0x0037,
        CREATEPLAYERVERIFY = 0x0038
    };

    struct DPAddress
    {
        DPAddress(std::uint32_t addr, std::uint16_t prt);

        std::string debugString() const;

        std::uint16_t port() const;
        void port(std::uint16_t port);

        std::uint32_t address() const;
        void address(std::uint32_t addr);

        std::uint16_t family;
        std::uint16_t _port;    // NB: in network byte order
        std::uint32_t _ipv4;    // NB: in network byte order
        std::uint8_t pad[8];

    };

    struct DPHeader
    {
        DPHeader(
            std::uint32_t replyAddress, std::uint16_t replyPort, const void* actionstring,
            DPlayCommandCode command, std::uint16_t dialect, std::size_t payloadSize);

        unsigned size() const;
        unsigned token() const;
        bool looksOk() const;

        std::uint32_t size_and_token;
        DPAddress address;
        char actionstring[4];
        std::uint16_t command;
        std::uint16_t dialect;
    };

    struct DPEnumReq
    {
        DPEnumReq(const std::uint8_t guid[16]);

        std::uint8_t guid[16];
        std::uint32_t passwordOffset;
        std::uint32_t flags;
        std::uint16_t password;
    };

    struct DPPackedPlayer
    {
        std::uint32_t size;
        std::uint32_t flags;
        std::uint32_t id;
        std::uint32_t short_name_length;
        std::uint32_t long_name_length;
        std::uint32_t service_provider_data_size;
        std::uint32_t player_data_size;
        std::uint32_t player_count;
        std::uint32_t system_player_id;
        std::uint32_t fixed_size;
        std::uint32_t dplay_version;
        std::uint32_t unknown;
    };

    struct DPSessionDescription
    {
        std::uint32_t size;
        std::uint32_t flags;
        char instanceGuid[16];
        char applicationGuid[16];
        std::uint32_t maxPlayers;
        std::uint32_t currentPlayerCount;
        std::uint32_t sessionNamePointerPlaceHolder;
        std::uint32_t passwordPointerPlaceHolder;
        std::uint32_t reserved[2];
        std::uint32_t applicationDefined[4];
    };

    struct DPSessionDescriptionChanged
    {
        std::uint32_t idFrom;
        std::uint32_t sessionNameOffset;
        std::uint32_t passwordOffset;
        DPSessionDescription sessionDescription;
        // char sessionName[]; // c wide string
        // char password[]; // c wide string
    };
}
