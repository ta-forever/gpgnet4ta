#pragma once

#include <cinttypes>
#include <functional>
#include <QtNetwork/qudpsocket.h>
#include <QtCore/qtimer.h>

#include "taflib/DuplicateDetection.h"
#include "taflib/nswfl_crc32.h"

namespace tafnet
{

    const std::uint32_t MAX_PACKET_SIZE_LOWER_LIMIT = 250;
    const int INITIAL_RESEND_TIMEOUT = 500; // milliseconds, until a ping measured
    const int MAX_RESEND_TIMEOUT = 700;     // milliseconds
    const int RESEND_TIMEOUT_MARGIN = 50;   // milliseconds, above measured ping
    const int MAX_RESEND_AT_ONCE = 5;
    const std::uint32_t PING_PACKET_SIZE = 16;
    const std::int64_t DEAD_PEER_TIMEOUT = 3 * 60 * 1000;    // milliseoncds, until give up pinging and delete their connection

    struct Payload
    {
        static const unsigned ACTION_INVALID = 0;

        // messages wrapped with TafnetMessageHeader
        static const unsigned ACTION_UDP_DATA = 2;
        static const unsigned ACTION_TCP_OPEN = 3;
        static const unsigned ACTION_TCP_CLOSE = 4;

        // messsages wrapped with TafnetBufferedHeader
        static const unsigned ACTION_TCP_DATA = 5;
        static const unsigned ACTION_TCP_ACK = 6;
        static const unsigned ACTION_TCP_RESEND = 7;
        static const unsigned ACTION_MORE = 8;
        static const unsigned ACTION_ENUM = 9;
        static const unsigned ACTION_UDP_PROTECTED = 10;
        static const unsigned ACTION_PACKSIZE_TEST = 11;
        static const unsigned ACTION_PACKSIZE_ACK = 12;
        static const unsigned ACTION_HELLO = 13;

        std::uint8_t action;
        QSharedPointer<QByteArray> buf;
        qint64 timestamp;

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

#pragma pack(push, 1)   // no padding
    struct TafnetMessageHeader
    {
      //std::uint32_t senderId;
        std::uint8_t action;
    };

    struct TafnetBufferedHeader
    {
      //std::uint32_t senderId;
        std::uint8_t action;
        std::uint32_t seq;
    };
#pragma pack(pop)

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
            std::uint32_t maxPacketSize = MAX_PACKET_SIZE_LOWER_LIMIT;

            std::int64_t timestampLastPing;
            std::int64_t timestampLastPingAck;
            std::int64_t timestampFirstPing;;
            std::deque<std::int64_t> recentPings;

            std::uint32_t lastTimeoutSeq;
            std::uint32_t lastResendReqSeq;

            ResendRate();
            int getResendRate(bool incSendCount);
            void registerAck();
            std::int64_t getSuccessfulPingTime();
        };
        std::map<std::uint32_t, ResendRate> m_resendRates;      // keyed by peer tafnet player id
        const std::uint32_t m_maxPacketSize;                    // upper limit on the otherwise auto-discovered UDP packet size
        const bool m_proactiveResendEnabled;

        // look for duplicate packets on UDP channel due to peers with activated proactive resend
        // (tcp channel takes care of itself)
        taflib::DuplicateDetection m_udpDuplicateDetection;

        // we disable resend requests after issueing them to avoid spamming.
        // they're reenabled on a timer
        std::map<std::uint32_t, BoolDefaultToTrue> m_resendRequestEnabled;
        QTimer m_resendReqReenableTimer;
        taflib::CRC32 m_crc32;

    public:
        TafnetNode(std::uint32_t playerId, bool isHost, QHostAddress bindAddress, quint16 bindPort, bool proactiveResend, std::uint32_t maxPacketSize);

        virtual void setHandler(const std::function<void(std::uint8_t, std::uint32_t, char*, int)>& f);
        virtual std::uint32_t getPlayerId() const;
        virtual std::uint32_t getHostPlayerId() const;
        virtual bool isHost() { return getPlayerId() == getHostPlayerId(); }
        virtual std::uint32_t maxPacketSizeForPlayerId(std::uint32_t id) const;
        virtual void sendPacksizeTests(std::uint32_t peerPlayerId);

        virtual void joinGame(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void connectToPeer(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId);
        virtual void disconnectFromPeer(std::uint32_t peerPlayerId);
        virtual void forwardGameData(std::uint32_t peerPlayerId, std::uint32_t action, const char* data, int len);

        virtual void onResendTimer();
        virtual void onResendReqReenableTimer();
        virtual void resetTcpBuffers();

        virtual void sendPingToPeers();
        virtual std::map<std::uint32_t, std::int64_t> getPingToPeers();

    private:
        virtual void onReadyRead();
        virtual void handleMessage(std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len);
        virtual void sendMessage(std::uint32_t peerPlayerId, std::uint32_t action, std::uint32_t seq, const char* data, int len, int nRepeats);
    };

}
