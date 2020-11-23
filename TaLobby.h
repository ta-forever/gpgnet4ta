#pragma once

#include <QtNetwork/qhostaddress.h>

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

    QSharedPointer<tafnet::TafnetNode> m_proxy;
    QSharedPointer<tafnet::TafnetGameNode> m_game;

public:
    TaLobby(
        QString lobbyBindAddress, QString gameReceiveBindAddress, QString gameAddress);

public slots:
    void onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal);
    void onJoinGame(QString host, QString playerName, int playerId);
    void onConnectToPeer(QString host, QString playerName, int playerId);
    void onDisconnectFromPeer(int playerId);
    void onRemoteGameSessionDetected();
};
