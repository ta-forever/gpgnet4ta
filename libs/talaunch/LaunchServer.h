#pragma once

#include "jdplay/JDPlay.h"
#include "tafgamestate.h"

#include <memory>
#include <windows.h>

#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtCore/qstring.h>
#include <QtNetwork/qnetworkaccessmanager.h>

namespace talaunch {

    class LaunchServer : public QObject
    {
        Q_OBJECT

        const int m_keepAliveTimeout;
        QTcpServer m_tcpServer;
        QList<QTcpSocket*> m_tcpSockets;
        std::shared_ptr<jdplay::JDPlay> m_jdPlay;
        int m_shutdownCounter;
        std::function<bool()> m_submitGameFileHashes;
        bool m_loggedAConnection;
        bool m_joinIsDisabled;
        QNetworkAccessManager m_nam;
        HANDLE       m_tafGameStateMap  = NULL;
        void*        m_tafGameStateView = NULL;
        TAFGameState m_tafGameStatePrev = {};
        QString      m_lastPlayerStatusMsg; // for log-spam suppression on heartbeat-only updates

    signals:
        void quit();
        void gameFailedToLaunch(QString gameGuid);
        void gameExitedWithError(quint32 exitCode);
        void gameFileVersionMismatch(QString message);

    public:
        LaunchServer(QHostAddress addr, quint16 port, int keepAliveTimeout);

    private:
        void onNewConnection();
        void onReadyReadTcp();
        void launchGame(QString _gameId, QString _guid, QString _player, QString _ipaddr,
            QString submitHashesEndPoint, QString submitHashesToken, bool asHost, bool doSearch);
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        void timerEvent(QTimerEvent* event);
        void notifyClients(QString msg);
        QString getGameFileHashes(int gameId, QString guid);
        void submitGameFileHashes(int gameId, int token, QString json, const QString endpoint, const QString accessToken);
        void openTAFGameState();
        void closeTAFGameState();
        void pollTAFGameState();
    };

}