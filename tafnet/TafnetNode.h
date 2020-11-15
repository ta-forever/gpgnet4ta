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

        std::uint32_t action : 3;
        std::uint32_t sourceId : 8;
        std::uint32_t destId : 8;
        std::uint32_t data_bytes : 13;
    };


    class TafnetNode : public QObject
    {
        const std::uint32_t m_tafnetId; // uniquely identifies this node
        QTcpServer m_tcpServer;         // listens for incoming connections from peer nodes
        QMap<QTcpSocket*, std::uint32_t> m_remoteTafnetIds; // peer ids keyed by socket
        QMap<std::uint32_t, QTcpSocket*> m_tcpSockets;      // sockets keyed by peer id
        std::function<void(const TafnetMessageHeader&, char*, int)> m_handleMessage; // optional hook for handleMessage

        virtual void onNewConnection();
        virtual void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        virtual void onReadyRead();
        virtual void sendHello(QTcpSocket* socket);
        virtual void handleMessage(const TafnetMessageHeader& tafheader, char* data, int len);

    public:
        TafnetNode(std::uint32_t tafnetId, QHostAddress bindAddress, quint16 bindPort);
        void setHandler(const std::function<void(const TafnetMessageHeader&, char*, int)>& f);
        std::uint32_t getTafnetId();
        bool connectToPeer(QHostAddress peer, quint16 peerPort);
        void forwardGameData(std::uint32_t destNodeId, std::uint32_t action, char* data, int len);
    };

}
