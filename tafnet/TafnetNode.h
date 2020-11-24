#pragma once

#include <cinttypes>
#include <functional>
#include <QtNetwork/qudpsocket.h>
#include <QtCore/qtimer.h>

namespace tafnet
{

    struct Payload
    {
        static const unsigned ACTION_INVALID = 0;

        static const unsigned ACTION_HELLO = 1;
        static const unsigned ACTION_UDP_DATA = 3;
        static const unsigned ACTION_TCP_OPEN = 4;
        static const unsigned ACTION_TCP_CLOSE = 5;

        static const unsigned ACTION_TCP_DATA = 6;
        static const unsigned ACTION_TCP_ACK = 7;
        static const unsigned ACTION_TCP_RESEND = 8;
        static const unsigned ACTION_TCP_SEQ_REBASE = 9;
        static const unsigned ACTION_ENUM = 10;

        std::uint8_t action;
        QSharedPointer<QByteArray> buf;

        Payload();
        void set(std::uint8_t action, const char *data, int len);
    };

    class DataBuffer
    {

        std::map<std::uint32_t, Payload > m_data;
        std::uint32_t m_nextPopSeq;
        std::uint32_t m_nextPushSeq;

    public:
        DataBuffer();
        void insert(std::uint32_t seq, std::uint8_t action, const char *data, int len);
        std::uint32_t push_back(std::uint8_t action, const char *data, int len);
        Payload pop();
        Payload get(std::uint32_t seq);
        std::map<std::uint32_t, Payload > & getAll();
        std::size_t size();
      
        void ackData(std::uint32_t seq);
        bool readyRead();
        std::uint32_t nextExpectedPopSeq();
    };

    struct TafnetMessageHeader
    {

        std::uint8_t action;
    };

    struct TafnetBufferedHeader
    {

        std::uint8_t action;
        std::uint32_t seq;
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

        QTimer m_resendTimer;

        const std::uint32_t m_playerId;
        std::uint32_t m_hostPlayerId;
        QUdpSocket m_lobbySocket;                               // send receive to peer TafnetNodes
        std::map<std::uint32_t, HostAndPort> m_peerAddresses;   // keyed by peer player id
        std::map<HostAndPort, std::uint32_t> m_peerPlayerIds;
        std::function<void(std::uint8_t, std::uint32_t, char*, int)> m_handleMessage; // optional hook for handleMessage

        std::map<std::uint32_t, DataBuffer> m_receiveBuffer;    // keyed by peer player id
        std::map<std::uint32_t, DataBuffer> m_sendBuffer;       // keyed by peer player id

        virtual void onReadyRead();
        virtual void handleMessage(std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len);
        virtual void sendMessage(std::uint32_t peerPlayerId, std::uint32_t action, std::uint32_t seq, const char* data, int len);

    public:
        TafnetNode(std::uint32_t playerId, bool isHost, QHostAddress bindAddress, quint16 bindPort);
        virtual void setHandler(const std::function<void(std::uint8_t, std::uint32_t, char*, int)>& f);
        virtual std::uint32_t getPlayerId() const;
        virtual std::uint32_t getHostPlayerId() const;
        virtual bool isHost() { return getPlayerId() == getHostPlayerId(); }

        virtual void joinGame(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void connectToPeer(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void disconnectFromPeer(std::uint32_t peerPlayerId);
        virtual void forwardGameData(std::uint32_t peerPlayerId, std::uint32_t action, const char* data, int len);

        virtual void onResendTimer();
    };

}
