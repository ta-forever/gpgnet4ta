#pragma once

#include <QtNetwork/qhostaddress.h>
#include <QtCore/quuid.h>
#include "GameEventHandlerQt.h"
#include "GameMonitor2.h"

namespace TADemo
{
    class TaPacketParser;
}

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

    QSharedPointer<tafnet::TafnetNode> m_proxy;             // communicates with other nodes via UDP port brokered by FAF ICE adapter
    QSharedPointer<tafnet::TafnetGameNode> m_game;          // bridge between m_proxy and TA instance
    QSharedPointer<TADemo::TAPacketParser> m_packetParser;  // snoops the network packets handled by m_game
    QSharedPointer<GameMonitor2> m_gameMonitor;             // infers major game events from packets provided by m_packetParser
    QSharedPointer<GameEventsSignalQt> m_gameEvents;        // translates inferred game events into Qt signals for external consumption

    QMap<QString, quint32> m_tafnetIdsByPlayerName;

public:
    TaLobby(QUuid gameGuid, QString lobbyBindAddress, QString gameReceiveBindAddress, QString gameAddress);

    void connectGameEvents(GameEventHandlerQt &subscriber);
    quint32 getLocalPlayerDplayId();
    quint32 getPlayerTafNetId(QString playerName);

public slots:
    void onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal);
    void onJoinGame(QString host, QString playerName, int playerId);
    void onConnectToPeer(QString host, QString playerName, int playerId);
    void onDisconnectFromPeer(int playerId);

public slots:
    void echoToGame(QString name, QString chat);
};
