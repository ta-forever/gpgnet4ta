#pragma once

#include <cinttypes>
#include <string>

namespace tafnet
{

    struct DPAddress
    {
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
        unsigned size() const;
        unsigned token() const;
        bool looksOk() const;

        std::uint32_t size_and_token;
        DPAddress address;
        char actionstring[4];
        std::uint16_t command;
        std::uint16_t dialect;
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

    class GameAddressTranslater
    {
        std::uint32_t newIpv4Address;
        std::uint16_t newPorts[2];

    public:
        GameAddressTranslater(std::uint32_t newIpv4Address, std::uint16_t _newPorts[2]);

        void operator()(char* buf, int len);
        bool translateHeader(char* buf, int len);
        void translateAddress(DPAddress& address, std::uint16_t newPort);
        bool translateForwardOrCreateRequest(char* buf, int len);
        bool translateSuperEnumPlayersReply(char* buf, int len);
    };

}
