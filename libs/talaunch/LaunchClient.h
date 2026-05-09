#pragma once

#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qhostaddress.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvector.h>

namespace talaunch {

    class LaunchClient : public QObject
    {
        Q_OBJECT

        QHostAddress m_serverAddress;
        quint16 m_serverPort;

        QTcpSocket m_tcpSocket;
        enum class State { CONNECTING, IDLE, RUNNING, LAUNCHED, FAIL };
        State m_state;

        QString m_playerName;
        QString m_gameGuid;
        int m_gameId;
        QString m_gameAddress;
        bool m_isHost;
        bool m_requireSearch;
        QString m_submitGameFileHashesEndpoint;
        QString m_submitGameFileHashesToken;
        QString m_lastPlayerStatusKey; // structural key (unit count compressed to 0/1) — log dedup

    signals:
        // Field semantics: see TAFGameState in tafgamestate.h (wire source).
        // Per-slot arrays are indexed by TA's local Players[0..9] order (local at slot 0).
        void playerStatusReceived(QVector<int> allyFlags, QVector<int> actives, QVector<int> unitCounts,
                                  QVector<int> allyTeams, QVector<int> propertyMasks, QVector<int> dplayIds);

    public:
        LaunchClient(QHostAddress addr, quint16 port);

        void setPlayerName(QString playerName);
        void setGameGuid(QString gameGuid);
        void setGameId(int gameId);
        void setAddress(QString address);
        void setIsHost(bool isHost);
        void setRequireSearch(bool requireSearch);  // false (default) for playing game, true for joining replay.  I don't know why
        void setSubmitGameFileHashesEndpoint(QString endpoint);
        void setSubmitGameFileHashesToken(QString token);

        bool startApplication();
        bool isApplicationRunning();
        bool isGameLaunched();          // players have progressed from the battleroom to the game
        bool failGameFileVersions(QString filename, QString reason);

    private:
        void onReadyReadTcp();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);

        bool connect(QHostAddress addr, quint16 port);
    };

}
