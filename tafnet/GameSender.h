#pragma once

#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qudpsocket.h>

namespace tafnet
{

    class GameSender : public QObject
    {
        QUdpSocket m_enumSocket;                // game responds to messages on this socket by advertising any hosted sessions
        QTcpSocket m_tcpSocket;                 // game receives tcp data on this socket, typically dplay session messages during game setup
        QSharedPointer<QUdpSocket> m_udpSocket; // shared with GameReceiver. game receives udp data on this socket, typically game data

        QHostAddress m_gameAddress;
        quint16 m_enumPort;
        quint16 m_tcpPort;
        quint16 m_udpPort;

    public:
        GameSender(QHostAddress gameAddress, quint16 enumPort);

        virtual void setTcpPort(quint16 port);
        virtual void setUdpPort(quint16 port);
        virtual void resetGameConnection();
        virtual void enumSessions(char* data, int len);
        virtual bool openTcpSocket(int timeoutMillisecond);
        virtual void sendTcpData(char* data, int len);
        virtual void closeTcpSocket();
        virtual void sendUdpData(char* data, int len);
        virtual QSharedPointer<QUdpSocket> getUdpSocket();
    };

}
