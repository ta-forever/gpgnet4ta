#include "TaLobby.h"

#include "tafnet/TafnetNode.h"
#include "tafnet/TafnetGameNode.h"

void SplitHostAndPort(QString hostAndPort, QHostAddress& host, quint16& port)
{
    QList<QString> parts = hostAndPort.split(':');
    if (parts.size() == 1)
    {
        host.setAddress(parts[0]);
    }
    else if (parts.size() > 1)
    {
        host.setAddress(parts[0]);
        port = parts[1].toInt();
    }
}

TaLobby::TaLobby(
    QString lobbyBindAddress, quint16 lobbyPortOverride,
    QString gameReceiveBindAddress, QString gameAddress):
    m_lobbyBindAddress(lobbyBindAddress),
    m_lobbyPortOverride(lobbyPortOverride),
    m_gameReceiveBindAddress(gameReceiveBindAddress),
    m_gameAddress(gameAddress)
{ }

void TaLobby::onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal)
{
    if (m_game)
    {
        return;
    }

    m_proxy.reset(new tafnet::TafnetNode(playerId, false, m_lobbyBindAddress, m_lobbyPortOverride ? m_lobbyPortOverride : localPort));
    m_game.reset(new tafnet::TafnetGameNode(
        m_proxy.data(),
        [this]() { return new tafnet::GameSender(this->m_gameAddress, 47624); },
        [this](tafnet::GameSender* sender) { return new tafnet::GameReceiver(this->m_gameReceiveBindAddress, 47624, 0, 0, sender);
    }));
}

void TaLobby::onJoinGame(QString _host, QString playerName, int playerId)
{
    if (!m_proxy || !m_game)
    {
        return;
    }

    QHostAddress host("127.0.0.1");
    quint16 port = 6112;
    SplitHostAndPort(_host, host, port);
    m_proxy->joinGame(host, port, playerId);
    m_game->registerRemotePlayer(playerId);
}

void TaLobby::onConnectToPeer(QString _host, QString playerName, int playerId)
{
    if (!m_proxy || !m_game)
    {
        return;
    }

    QHostAddress host("127.0.0.1");
    quint16 port = 6112;
    SplitHostAndPort(_host, host, port);
    m_proxy->connectToPeer(host, port, playerId);
    m_game->registerRemotePlayer(playerId);
}
