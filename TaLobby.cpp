#include "TaLobby.h"

#include "tafnet/TafnetNode.h"
#include "tafnet/TafnetGameNode.h"
#include "tademo/Watchdog.h"
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
    QUuid gameGuid, QString lobbyBindAddress, QString gameReceiveBindAddress, QString gameAddress, bool proactiveResend):
    m_lobbyBindAddress("127.0.0.1"),
    m_lobbyPortOverride(0),
    m_gameReceiveBindAddress(gameReceiveBindAddress),
    m_gameAddress(gameAddress),
    m_gameGuid(gameGuid),
    m_proactiveResendEnabled(proactiveResend)
{
    SplitHostAndPort(lobbyBindAddress, m_lobbyBindAddress, m_lobbyPortOverride);
    m_gameEvents.reset(new GameEventsSignalQt());
    m_gameMonitor.reset(new GameMonitor2(m_gameEvents.data(), TICKS_TO_GAME_START, TICKS_TO_GAME_DRAW));
    m_packetParser.reset(new TADemo::TAPacketParser(m_gameMonitor.data()));
}

void TaLobby::connectGameEvents(GameEventHandlerQt &subscriber)
{
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::gameSettings, &subscriber, &GameEventHandlerQt::onGameSettings);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::playerStatus, &subscriber, &GameEventHandlerQt::onPlayerStatus);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::clearSlot, &subscriber, &GameEventHandlerQt::onClearSlot);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::gameStarted, &subscriber, &GameEventHandlerQt::onGameStarted);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::gameEnded, &subscriber, &GameEventHandlerQt::onGameEnded);
    QObject::connect(m_gameEvents.data(), &GameEventsSignalQt::chat, &subscriber, &GameEventHandlerQt::onChat);
}

quint32 TaLobby::getLocalPlayerDplayId()
{
    if (m_gameMonitor)
    {
        return m_gameMonitor->getLocalPlayerDplayId();
    }
    else
    {
        return 0u;
    }
}

void TaLobby::onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal)
{
    try
    {
        TADemo::Watchdog wd("TaLobby::onCreateLobby", 100);
        if (m_game)
        {
            return;
        }

        m_tafnetIdsByPlayerName[playerName] = playerId;
        m_gameMonitor->setHostPlayerName(playerName.toStdString()); // assume we're host until call to onJoinGame() indicates otherwise
        m_gameMonitor->setLocalPlayerName(playerName.toStdString()); // this won't change
        m_proxy.reset(new tafnet::TafnetNode(
            playerId, false, m_lobbyBindAddress, m_lobbyPortOverride ? m_lobbyPortOverride : localPort, m_proactiveResendEnabled));
        m_game.reset(new tafnet::TafnetGameNode(
            m_proxy.data(),
            m_packetParser.data(),
            [this]() { return new tafnet::GameSender(this->m_gameAddress, 47624); },
            [this](QSharedPointer<QUdpSocket> udpSocket) { return new tafnet::GameReceiver(this->m_gameReceiveBindAddress, 0, 0, udpSocket);
        }));
    }
    catch (std::exception &e)
    {
        qWarning() << "[TaLobby::onCreateLobby] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaLobby::onCreateLobby] unknown exception";
    }
}

void TaLobby::onJoinGame(QString _host, QString playerName, int playerId)
{
    try
    {
        TADemo::Watchdog wd("TaLobby::onJoinGame", 100);
        if (!m_proxy || !m_game)
        {
            return;
        }

        m_tafnetIdsByPlayerName[playerName] = playerId;
        QHostAddress host("127.0.0.1");
        quint16 port = 6112;
        SplitHostAndPort(_host, host, port);
        m_gameMonitor->setHostPlayerName(playerName.toStdString());
        m_proxy->joinGame(host, port, playerId);
        m_game->registerRemotePlayer(playerId, 47624);
    }
    catch (std::exception &e)
    {
        qWarning() << "[TaLobby::onJoinGame] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaLobby::onJoinGame] unknown exception";
    }
}

void TaLobby::onConnectToPeer(QString _host, QString playerName, int playerId)
{
    try
    {
        TADemo::Watchdog wd("TaLobby::onConnectToPeer", 100);
        if (!m_proxy || !m_game)
        {
            return;
        }

        m_tafnetIdsByPlayerName[playerName] = playerId;
        QHostAddress host("127.0.0.1");
        quint16 port = 6112;
        SplitHostAndPort(_host, host, port);
        m_proxy->connectToPeer(host, port, playerId);
        m_game->registerRemotePlayer(playerId, 0);
    }
    catch (std::exception &e)
    {
        qWarning() << "[TaLobby::onConnectToPeer] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaLobby::onConnectToPeer] unknown exception";
    }
}

void TaLobby::onDisconnectFromPeer(int playerId)
{
    try
    {
        TADemo::Watchdog wd("TaLobby::onDisconnectFromPeer", 100);
        qDebug() << "[TaLobby::onDisconnectFromPeer]" << playerId;
        if (!m_proxy || !m_game)
        {
            return;
        }

        m_proxy->disconnectFromPeer(playerId);
        m_game->unregisterRemotePlayer(playerId);
    }
    catch (std::exception &e)
    {
        qWarning() << "[TaLobby::onDisconnectFromPeer] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaLobby::onDisconnectFromPeer] unknown exception";
    }
}

void TaLobby::echoToGame(bool isPrivate, QString name, QString chat)
{
    const bool fromIngameBot = name.endsWith("[ingame]");
    if (fromIngameBot)
    {
        // game has already seen this message
        return;
    }

    std::uint32_t dplayId = 0;
    std::uint32_t tafnetId = 0;
    if (m_tafnetIdsByPlayerName.count(name) > 0)
    {
        tafnetId = m_tafnetIdsByPlayerName[name];
    }
    if (m_gameMonitor)
    {
        for (const std::string& playerName : m_gameMonitor->getPlayerNames(true, true))
        {
            if (playerName != m_gameMonitor->getLocalPlayerName())
            {
                // choose a dplayId for any remote player to ensure IRC chat gets relayed into TA even if its the local player typing
                dplayId = m_gameMonitor->getPlayerData(playerName).dplayid;
                break;
            }
        }
    }

    m_game->messageToLocalPlayer(dplayId, tafnetId, isPrivate, name.toStdString(), chat.toStdString());
}
