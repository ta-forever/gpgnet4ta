#include "TaLobby.h"

#include "tafnet/TafnetNode.h"
#include "tafnet/TafnetGameNode.h"
#include "tademo/TAPacketParser.h"

static const std::uint32_t TICKS_TO_GAME_START = 1800;  // 60 sec
static const std::uint32_t TICKS_TO_GAME_DRAW = 60;     // 2 sec

static void SplitHostAndPort(QString hostAndPort, QHostAddress& host, quint16& port)
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
    QString lobbyBindAddress, QString gameReceiveBindAddress, QString gameAddress):
    m_lobbyBindAddress("127.0.0.1"),
    m_lobbyPortOverride(0),
    m_gameReceiveBindAddress(gameReceiveBindAddress),
    m_gameAddress(gameAddress)
{
    SplitHostAndPort(lobbyBindAddress, m_lobbyBindAddress, m_lobbyPortOverride);
    m_gameEvents.reset(new GameEventsSignalQt());
    m_gameMonitor.reset(new GameMonitor2(m_gameEvents.data(), TICKS_TO_GAME_START, TICKS_TO_GAME_DRAW));
    m_packetParser.reset(new TADemo::TAPacketParser(m_gameMonitor.data(), true));
}

void TaLobby::subscribeGameEvents(GameEventHandlerQt &subscriber)
{
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::gameSettings, &subscriber, &GameEventHandlerQt::onGameSettings);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::playerStatus, &subscriber, &GameEventHandlerQt::onPlayerStatus);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::gameStarted, &subscriber, &GameEventHandlerQt::onGameStarted);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::gameEnded, &subscriber, &GameEventHandlerQt::onGameEnded);
}

void TaLobby::onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal)
{
    if (m_game)
    {
        return;
    }

    m_gameMonitor->setHostPlayerName(playerName.toStdString()); // assume we're host until call to onJoinGame() indicates otherwise
    m_proxy.reset(new tafnet::TafnetNode(playerId, false, m_lobbyBindAddress, m_lobbyPortOverride ? m_lobbyPortOverride : localPort));
    m_game.reset(new tafnet::TafnetGameNode(
        m_proxy.data(),
        m_packetParser.data(),
        [this]() { return new tafnet::GameSender(this->m_gameAddress, 47624); },
        [this](QSharedPointer<QUdpSocket> udpSocket) { return new tafnet::GameReceiver(this->m_gameReceiveBindAddress, 0, 0, udpSocket);
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
    m_gameMonitor->setHostPlayerName(playerName.toStdString());
    m_proxy->joinGame(host, port, playerId);
    m_game->registerRemotePlayer(playerId, 47624);
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
    m_game->registerRemotePlayer(playerId, 0);
}

void TaLobby::onDisconnectFromPeer(int playerId)
{
    qDebug() << "[TaLobby::onDisconnectFromPeer]" << playerId;
    if (!m_proxy || !m_game)
    {
        return;
    }

    m_proxy->disconnectFromPeer(playerId);
    m_game->unregisterRemotePlayer(playerId);
}
