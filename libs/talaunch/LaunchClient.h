#pragma once

#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qhostaddress.h>
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

    signals:
        // allyFlags is 10x10 row-major: allyFlags[i*10+j] != 0 => slot i allied with slot j
        // allyTeams: PlayerStruct.AllyTeam per slot (0-4 = explicit team, 5 = none)
        // propertyMasks: PlayerInfoStruct.PropertyMask per slot (WATCH=0x40, HUMANPLAYER=0x80, PLAYERCHEATING=0x2000)
        // infoTypes/myTypes: PlayerInfoStruct.PlayerType / PlayerStruct.My_PlayerType (0=none,1=LocalHuman,2=LocalAI,3=RemoteHuman,4=RemoteAI)
        void playerStatusReceived(QVector<int> allyFlags, QVector<int> actives, QVector<int> allyTeams,
                                  QVector<int> raceSides, QVector<int> propertyMasks,
                                  QVector<int> infoTypes, QVector<int> myTypes,
                                  QVector<int> dplayIds, QVector<int> winLoseTimes, QVector<int> unitsNumbers);

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