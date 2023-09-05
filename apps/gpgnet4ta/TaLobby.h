#pragma once

#include <QtNetwork/qhostaddress.h>
#include <QtCore/quuid.h>
#include <QtCore/qtimer.h>
#include "GameEventHandlerQt.h"
#include "GameMonitor2.h"
#include "tareplay/TaDemoCompilerClient.h"

namespace tafnet
{
    class TafnetNode;
    class TafnetGameNode;
}

class TaLobby : public QObject
{
    Q_OBJECT

    QHostAddress m_lobbyBindAddress;
    quint16 m_lobbyPortOverride;
    const QHostAddress m_gameReceiveBindAddress;
    const QHostAddress m_gameAddress;
    const QUuid m_gameGuid;
    const bool m_proactiveResendEnabled;
    const std::uint32_t m_maxPacketSize;
    QTimer m_pingTimer;

    QSharedPointer<tafnet::TafnetNode> m_proxy;             // communicates with other nodes via UDP port brokered by FAF ICE adapter
    QSharedPointer<tafnet::TafnetGameNode> m_game;          // bridge between m_proxy and TA instance
    QSharedPointer<tapacket::TAPacketParser> m_packetParser;  // snoops the network packets handled by m_game
    QSharedPointer<GameMonitor2> m_gameMonitor;             // infers major game events from packets provided by m_packetParser
    QSharedPointer<GameEventsSignalQt> m_gameEvents;        // translates inferred game events into Qt signals for external consumption
    QSharedPointer<tareplay::TaDemoCompilerClient> m_taDemoCompilerClient;          // submits data to TaDemoCompiler server for live replay

    QMap<QString, quint32> m_tafnetIdsByPlayerName;

public:
    TaLobby(QUuid gameGuid, QString lobbyBindAddress, QString gameReceiveBindAddress, QString gameAddress, bool proactiveResend, quint32 maxPacketSize);
    void enableForwardToDemoCompiler(QString hostName, quint16 port, quint32 tafGameId);

    void connectGameEvents(GameEventHandlerQt &subscriber);
    quint32 getLocalPlayerDplayId();
    void setPeerPingInterval(int milliseconds);

signals:
    void peerPingStats(QMap<quint32, qint64> pingsPerPeer);

public slots:
    void onCreateLobby(int protocol, int localPort, QString playerAlias, QString playerRealName, int playerId, int natTraversal);
    void onJoinGame(QString host, QString playerAlias, QString playerRealName, int playerId);
    void onConnectToPeer(QString host, QString playerAlias, QString playerRealName, int playerId);
    void onDisconnectFromPeer(int playerId);
    void onExtendedMessage(QString msg);

public slots:
    void echoToGame(bool isPrivate, QString name, QString chat);
};
