#pragma once

#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qhostaddress.h>

namespace talaunch {

    class LaunchClient : public QObject
    {
        QHostAddress m_serverAddress;
        quint16 m_serverPort;

        QTcpSocket m_tcpSocket;
        enum class State { CONNECTING, IDLE, RUNNING, FAIL };
        State m_state;

        QString m_playerName;
        QString m_gameGuid;
        QString m_gameAddress;
        bool m_isHost;

    public:
        LaunchClient(QHostAddress addr, quint16 port);

        void setPlayerName(QString playerName);
        void setGameGuid(QString gameGuid);
        void setAddress(QString address);
        void setIsHost(bool isHost);

        bool launch();
        bool isRunning();

    private:
        void onReadyReadTcp();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);

        bool connect(QHostAddress addr, quint16 port);
    };

}