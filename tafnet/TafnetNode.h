#pragma once

#include <cinttypes>
#include <functional>
#include <QtNetwork/qudpsocket.h>
#include <QtCore/qtimer.h>

#include "tademo/DuplicateDetection.h"

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
        static const unsigned ACTION_MORE = 9;
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
        void reset();
        void insert(std::uint32_t seq, std::uint8_t action, const char *data, int len);
        std::uint32_t push_back(std::uint8_t action, const char *data, int len);
        Payload pop();
        Payload get(std::uint32_t seq);
        std::map<std::uint32_t, Payload > & getAll();
        std::size_t size();
      
        bool ackData(std::uint32_t seq);
        bool readyRead();
        bool empty();
        std::uint32_t nextExpectedPopSeq();
        std::uint32_t earliestAvailable();
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

            HostAndPort();
            HostAndPort(QHostAddress addr, std::uint16_t port);
            bool operator< (const HostAndPort& other) const;
        };

        struct BoolDefaultToTrue
        {
            bool value = true;
        };

        QTimer m_resendTimer;

        const std::uint32_t m_playerId;
        std::uint32_t m_hostPlayerId;
        QUdpSocket m_lobbySocket;                               // send/receive to/from peer TafnetNodes
        std::map<std::uint32_t, HostAndPort> m_peerAddresses;   // keyed by peer tafnet player id
        std::map<HostAndPort, std::uint32_t> m_peerPlayerIds;
        std::function<void(std::uint8_t, std::uint32_t, char*, int)> m_handleMessage; // optional hook for handleMessage

        std::map<std::uint32_t, DataBuffer> m_receiveBuffer;    // keyed by peer tafnet player id
        std::map<std::uint32_t, DataBuffer> m_sendBuffer;       // keyed by peer tafnet player id
        std::map<std::uint32_t, QByteArray> m_reassemblyBuffer;

        // we maintain stats of how many times a packet is sent before we receive an ACK for it
        // then if we find packet loss is high we start to spam the packets
        // (or maybe more PC we "proactively" resend)
        struct ResendRate
        {
            std::uint32_t sendCount = 0u;
            std::uint32_t ackCount = 0u;
            int get(bool incSendCount);
        };
        std::map<std::uint32_t, ResendRate> m_resendRates;      // keyed by peer tafnet player id
        const bool m_proactiveResendEnabled;

        // look for duplicate packets on UDP channel due to peers with activated proactive resend
        // (tcp channel takes care of itself)
        TADemo::DuplicateDetection m_udpDuplicateDetection;

        // we disable resend requests after issueing them to avoid spamming.
        // they're reenabled on a timer
        std::map<std::uint32_t, BoolDefaultToTrue> m_resendRequestEnabled;
        QTimer m_resendReqReenableTimer;

    public:
        TafnetNode(std::uint32_t playerId, bool isHost, QHostAddress bindAddress, quint16 bindPort, bool proactiveResend);

        virtual void setHandler(const std::function<void(std::uint8_t, std::uint32_t, char*, int)>& f);
        virtual std::uint32_t getPlayerId() const;
        virtual std::uint32_t getHostPlayerId() const;
        virtual bool isHost() { return getPlayerId() == getHostPlayerId(); }

        virtual void joinGame(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void connectToPeer(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void disconnectFromPeer(std::uint32_t peerPlayerId);
        virtual void forwardGameData(std::uint32_t peerPlayerId, std::uint32_t action, const char* data, int len);

        virtual void onResendTimer();
        virtual void onResendReqReenableTimer();
        virtual void resetTcpBuffers();

    private:
        virtual void onReadyRead();
        virtual void handleMessage(std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len);
        virtual void sendMessage(std::uint32_t peerPlayerId, std::uint32_t action, std::uint32_t seq, const char* data, int len, int nRepeats);
    };

}
