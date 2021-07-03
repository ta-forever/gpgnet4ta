#pragma once

#include "jdplay/JDPlay.h"

#include <memory>

#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>


class LaunchServer : public QObject
{
    Q_OBJECT

    QTcpServer m_tcpServer;
    QList<QTcpSocket*> m_tcpSockets;
    std::shared_ptr<JDPlay> m_jdPlay;
    int m_shutdownCounter;
    bool m_loggedAConnection;

signals:
    void quit();
    void gameFailedToLaunch(QString gameGuid);
    void gameExitedWithError(quint32 exitCode);

public:
    LaunchServer(QHostAddress addr, quint16 port);

private:

    void onNewConnection();
    void onReadyReadTcp();
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);
    void timerEvent(QTimerEvent* event);
    void launchGame(QString gameGuid, QString playerName, QString gameAddress, bool asHost);
    void notifyClients(QString msg);
};
