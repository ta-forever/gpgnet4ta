#pragma once

#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qhostaddress.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvector.h>

namespace talaunch {

    // Mirrors TAFKillEvent in tafgamestate.h, projected through the wire protocol.
    // wallClockMs / dplayIds / flags are decoded from the per-event ":-separated tokens.
    struct KillEventQt
    {
        qint64  wallClockMs;
        quint32 victimDplayId;
        quint32 killerDplayId;
        quint16 flags;          // see TAF_KILL_FLAG_* in tafgamestate.h
    };

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
        QString m_lastPlayerStatusKey; // structural-only key (unit count compressed to 0/1)
                                       // for log-spam suppression on heartbeat AND unit-count-drift PLAYER_STATUS.
        quint32 m_lastKillRingHead;    // last-seen head counter; new events have head > this
        quint32 m_lastTiebreakerWinnerDpid;  // last-seen non-zero tiebreaker winner; suppresses re-emit

    signals:
        // allyFlags is 10x10 row-major: allyFlags[i*10+j] != 0 => slot i allied with slot j
        // actives: 1 = slot occupied (player joined), 0 = empty slot
        // unitCounts: live unit count per slot, from the engine's local-view bookkeeping
        //   (PlayerStruct.UnitsNumber). Consumers derive elimination via a max-seen-then-zero
        //   edge latch.
        // allyTeams: PlayerStruct.AllyTeam per slot (0-4 = explicit team, 5 = none)
        // propertyMasks: PlayerInfoStruct.PropertyMask per slot (WATCH=0x40, HUMANPLAYER=0x80, PLAYERCHEATING=0x2000)
        //
        // NOTE: per-slot arrays are indexed by TA's local Players[0..9] order — each peer puts
        // itself at slot 0. Resolve players by dplayIds[xslot], not by lobby slot.
        void playerStatusReceived(QVector<int> allyFlags, QVector<int> actives, QVector<int> unitCounts,
                                  QVector<int> allyTeams, QVector<int> propertyMasks, QVector<int> dplayIds);

        // Fired when one or more new kill events arrive (i.e. the producer's head counter
        // advanced since the last poll). `events` is in chronological order — oldest first.
        // Listeners typically buffer these and consult them at game-end-resolution time.
        void killEventsReceived(QVector<talaunch::KillEventQt> events);

        // Fired when tdraw's local tiebreaker resolves a mutual-elim end-game.
        // winnerDplayId is the dplayId of the winning player (skip GameMonitor2's own
        // tiebreaker). Only fires once per game (de-duped on the receive side).
        void tiebreakerWinnerReceived(quint32 winnerDplayId);

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

Q_DECLARE_METATYPE(talaunch::KillEventQt)
Q_DECLARE_METATYPE(QVector<talaunch::KillEventQt>)