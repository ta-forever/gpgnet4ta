#pragma once

#include "jdplay/JDPlay.h"

#include <memory>

#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>

namespace talaunch {

    class LaunchServer : public QObject
    {
        Q_OBJECT

        const int m_keepAliveTimeout;
        QTcpServer m_tcpServer;
        QList<QTcpSocket*> m_tcpSockets;
        std::shared_ptr<jdplay::JDPlay> m_jdPlay;
        int m_shutdownCounter;
        bool m_loggedAConnection;
        bool m_joinIsDisabled;

    signals:
        void quit();
        void gameFailedToLaunch(QString gameGuid);
        void gameExitedWithError(quint32 exitCode);

    public:
        LaunchServer(QHostAddress addr, quint16 port, int keepAliveTimeout);

    private:

        void onNewConnection();
        void onReadyReadTcp();
        void launchGame(QString _guid, QString _player, QString _ipaddr, bool asHost, bool doSearch);
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        void timerEvent(QTimerEvent* event);
        void notifyClients(QString msg);
    };

}