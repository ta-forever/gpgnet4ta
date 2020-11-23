#pragma once

#include <cinttypes>
#include <functional>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qudpsocket.h>

namespace tafnet
{

    struct TafnetMessageHeader
    {
        static const unsigned ACTION_HELLO = 1;
        static const unsigned ACTION_ENUM = 2;
        static const unsigned ACTION_TCP_OPEN = 3;
        static const unsigned ACTION_TCP_DATA = 4;
        static const unsigned ACTION_TCP_CLOSE = 5;
        static const unsigned ACTION_UDP_DATA = 6;

        std::uint8_t action;
    };

    class TafnetNode : public QObject
    {
        struct HostAndPort
        {
            std::uint32_t ipv4addr;
            std::uint16_t port;

            HostAndPort() :
                ipv4addr(0),
                port(0)
            { }

            HostAndPort(QHostAddress addr, std::uint16_t port) :
                ipv4addr(addr.toIPv4Address()),
                port(port)
            { }

            bool operator< (const HostAndPort& other) const
            {
                return (port < other.port) || (port == other.port) && (ipv4addr < other.ipv4addr);
            }
        };

        const std::uint32_t m_playerId;
        std::uint32_t m_hostPlayerId;
        QUdpSocket m_lobbySocket;                               // send receive to peer TafnetNodes
        std::map<std::uint32_t, HostAndPort> m_peerAddresses;   // keyed by peer player id
        std::map<HostAndPort, std::uint32_t> m_peerPlayerIds;
        std::function<void(const TafnetMessageHeader&, std::uint32_t, char*, int)> m_handleMessage; // optional hook for handleMessage

        virtual void onReadyRead();
        virtual void handleMessage(const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len);

    public:
        TafnetNode(std::uint32_t playerId, bool isHost, QHostAddress bindAddress, quint16 bindPort);
        virtual void setHandler(const std::function<void(const TafnetMessageHeader&, std::uint32_t, char*, int)>& f);
        virtual std::uint32_t getPlayerId() const;
        virtual std::uint32_t getHostPlayerId() const;

        virtual void joinGame(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void connectToPeer(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void disconnectFromPeer(std::uint32_t peerPlayerId);
        virtual void forwardGameData(std::uint32_t peerPlayerId, std::uint32_t action, const char* data, int len);
    };

}
