#pragma once

#include <functional>

#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qudpsocket.h>

namespace tafnet
{
    class GameSender;

    class GameReceiver : public QObject
    {
        QHostAddress m_bindAddress;
        quint16 m_enumPort;         // we'll listen for enumeration requests from game on this port
        quint16 m_tcpPort;          // we'll listen for game tcp data (typically dplay game setup data) on this port
        quint16 m_udpPort;          // we'll listen for game udp data (typically actual game data) on this port
        GameSender* m_sender;       // we need to inform the sender of the ports that the game is using which we only discover once we've received some data from it

        QTcpServer m_tcpServer;
        QTcpServer m_enumServer;
        QSharedPointer<QUdpSocket> m_udpSocket;
        QList<QAbstractSocket*> m_sockets;   // those associated with m_tcpServer, and also those not
        std::function<void(QAbstractSocket*, int, char*, int)> m_handleMessage; // optional hook for handleMessage

        virtual int getChannelCodeFromSocket(QAbstractSocket* socket);
        virtual void onNewConnection();
        virtual void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        virtual void onReadyReadTcp();
        virtual void onReadyReadUdp();
        virtual void handleMessage(QAbstractSocket* receivingSocket, int channel, char* data, int len);

    public:
        static const int CHANNEL_ENUM = 1;
        static const int CHANNEL_TCP = 2;
        static const int CHANNEL_UDP = 3;

        GameReceiver(QHostAddress bindAddress, quint16 enumPort, quint16 tcpPort, quint16 udpPort, QSharedPointer<QUdpSocket> udpSocket);
        void setHandler(const std::function<void(QAbstractSocket*, int, char*, int)>& f);
        QHostAddress getBindAddress();
        quint16 getEnumListenPort();
        quint16 getTcpListenPort();
        quint16 getUdpListenPort();
        quint16* getListenPorts(quint16 ports[2]);
    };
}
