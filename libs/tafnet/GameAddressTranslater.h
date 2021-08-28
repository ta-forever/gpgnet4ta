#pragma once

#include <cinttypes>
#include <functional>
#include <map>
#include <string>

namespace tapacket
{
    struct DPAddress;
}

namespace tafnet
{
    class GameAddressTranslater
    {
        std::uint32_t replyAddress;
        std::uint16_t replyPorts[2];
        std::function<bool(tapacket::DPAddress & address, int /* 0:tcp, 1:udp */)> translatePlayerSPA;

    public:
        typedef std::function<bool(tapacket::DPAddress & address, int /* 0:tcp, 1:udp */)> TranslatePlayerSPA;

        GameAddressTranslater(std::uint32_t replyAddress, const std::uint16_t replyPorts[]);

        GameAddressTranslater(
            std::uint32_t replyAddress, const std::uint16_t replyPorts[],
            const TranslatePlayerSPA & translatePlayerSPAddress);

        void operator()(char* buf, int len) const;

        bool translateHeader(char* buf, int len) const;
        bool translateForwardOrCreateRequest(char* buf, int len) const;
        bool translateSuperEnumPlayersReply(char* buf, int len) const;

    private:
        void translateReplyAddress(tapacket::DPAddress& address, int index) const;
    };

}
