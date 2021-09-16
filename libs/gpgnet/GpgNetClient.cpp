#include "GpgNetClient.h"
#include "GpgNetParse.h"
#include "GpgNetServerMessages.h"
#include "taflib/Watchdog.h"

using namespace gpgnet;

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

GpgNetClient::GpgNetClient(QString gpgnetHostAndPort) :
GpgNetSend(m_datastream),
m_datastream(&m_socket)
{
    qInfo() << "[GpgNetClient::GpgNetClient] constructing";
    QHostAddress gpgnetHost("127.0.0.1");
    quint16 gpgnetPort = 0xdead;
    SplitHostAndPort(gpgnetHostAndPort, gpgnetHost, gpgnetPort);

    m_datastream.setByteOrder(QDataStream::LittleEndian);
    m_socket.connectToHost(gpgnetHost, gpgnetPort);
    if (!m_socket.waitForConnected(3000))
    {
        throw std::runtime_error("unable to connect to gpgnet");
    }
    qInfo() << "[GpgNetClient::GpgNetClient] connect" << m_socket.peerAddress().toString() << ':' << m_socket.peerPort();
    QObject::connect(&m_socket, &QTcpSocket::readyRead, this, &GpgNetClient::onReadyRead);
    QObject::connect(&m_socket, &QTcpSocket::stateChanged, this, &GpgNetClient::onSocketStateChanged);
}

void GpgNetClient::onReadyRead()
{
    try
    {
        taflib::Watchdog wd("GpgNetClient::onReadyRead", 100);
        QAbstractSocket* sender = static_cast<QAbstractSocket*>(QObject::sender());
        while (sender->bytesAvailable() > 0)
        {
            QVariantList serverCommand = m_gpgNetParser.GetCommand(m_datastream);
            QString cmd = serverCommand[0].toString();
            qInfo() << "[GpgNetClient::onReadyRead] gpgnet command received:" << cmd;

            if (cmd == CreateLobbyCommand::ID)
            {
                CreateLobbyCommand clc;
                clc.Set(serverCommand);
                m_gpgnetPlayerIds[clc.playerAlias] = clc.playerId;
                emit createLobby(
                    clc.protocol, clc.localPort, clc.playerAlias, clc.playerRealName,
                    clc.playerId, clc.natTraversal);
            }
            else if (cmd == HostGameCommand::ID)
            {
                HostGameCommand hgc;
                hgc.Set(serverCommand);
                emit hostGame(hgc.mapName);
            }
            else if (cmd == JoinGameCommand::ID)
            {
                JoinGameCommand jgc(serverCommand);
                m_gpgnetPlayerIds[jgc.remotePlayerAlias] = jgc.remotePlayerId;
                qInfo() << "[GpgNetClient::onReadyRead] join game: playername=" << jgc.remotePlayerAlias << "playerId=" << jgc.remotePlayerId;
                emit joinGame(jgc.remoteHost, jgc.remotePlayerAlias, jgc.remotePlayerRealName, jgc.remotePlayerId);
            }
            else if (cmd == ConnectToPeerCommand::ID)
            {
                ConnectToPeerCommand ctp(serverCommand);
                m_gpgnetPlayerIds[ctp.playerAlias] = ctp.playerId;
                qInfo() << "[GpgNetClient::onReadyRead] connect to peer: playername=" << ctp.playerAlias << "playerId=" << ctp.playerId;
                emit connectToPeer(ctp.host, ctp.playerAlias, ctp.playerRealName, ctp.playerId);
            }
            else if (cmd == DisconnectFromPeerCommand::ID)
            {
                DisconnectFromPeerCommand ctp(serverCommand);
                qInfo() << "[GpgNetClient::onReadyRead] disconnect from peer: playerid=" << ctp.playerId;
                //gpgPlayerIds erase where value == ctp.playerId; // not super important
                emit disconnectFromPeer(ctp.playerId);
            }
        }
    }
    catch (const gpgnet::GpgNetParse::DataNotReady &)
    { }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetClient::onReadyRead] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetClient::onReadyRead] unknown exception";
    }
}

quint32 GpgNetClient::lookupPlayerId(QString playerName)
{
    if (playerName.startsWith("AI:"))
    {
        return 0u;
    }

    auto it = m_gpgnetPlayerIds.find(playerName);
    if (it == m_gpgnetPlayerIds.end())
    {
        qWarning() << "[GpgNetClient::lookupPlayerId] unknown player:" << playerName;
        return 0u;
    }
    return it.value();
}

void GpgNetClient::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        taflib::Watchdog wd("GpgNetClient::onSocketStateChanged", 100);
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            qInfo() << "[GpgNetClient::onSocketStateChanged/UnconnectedState]" << sender->peerAddress().toString() << ":" << sender->peerPort();
        }
    }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetClient::onSocketStateChanged] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetClient::onSocketStateChanged] unknown exception";
    }
}

void GpgNetClient::sendGameState(QString state, QString substate)
{
    // ICE adapter drops 2nd GameState argument. So here we just bung the substate into a GameOption beforehand
    // ..... @todo work out how to build the ICE adapter so we can customise it for our own purposes
    sendCommand("GameOption", 2);
    sendArgument("SubState");
    sendArgument(substate.toUtf8());
    sendCommand("GameState", 1);
    sendArgument(state.toUtf8());
}

void GpgNetClient::sendCreateLobby(int /* eg 0 */, int /* eg 0xb254 */, const char* playerName, int /* eg 0x9195 */, int /* eg 1 */)
{
    throw std::runtime_error("not implemented");
}

void GpgNetClient::sendHostGame(QString mapName)
{
    sendCommand(HostGameCommand::ID, 1);
    sendArgument(mapName.toUtf8());
}

void GpgNetClient::sendJoinGame(QString hostAndPort, QString remotePlayerName, int remotePlayerId)
{
    sendCommand(JoinGameCommand::ID, 3);
    sendArgument(remotePlayerName.toUtf8());
    sendArgument(remotePlayerId);
}

void GpgNetClient::sendGameMods(int numMods)
{
    sendCommand("GameMods", 2);
    sendArgument("activated");
    sendArgument(numMods);
}

void GpgNetClient::sendGameMods(QStringList uids)
{
    sendCommand("GameMods", 2);
    sendArgument("uids");
    sendArgument(uids.join(' ').toUtf8());
}

void GpgNetClient::sendGameOption(QString key, QString value)
{
    sendCommand("GameOption", 2);
    sendArgument(key.toUtf8());
    sendArgument(value.toUtf8());
}

void GpgNetClient::sendGameOption(QString key, int value)
{
    sendCommand("GameOption", 2);
    sendArgument(key.toUtf8());
    sendArgument(value);
}

void GpgNetClient::sendPlayerOption(QString playerId, QString key, QString value)
{
    sendCommand("PlayerOption", 3);
    sendArgument(playerId.toUtf8());
    sendArgument(key.toUtf8());
    sendArgument(value.toUtf8());
}

void GpgNetClient::sendPlayerOption(QString playerId, QString key, int value)
{
    sendCommand("PlayerOption", 3);
    sendArgument(playerId.toUtf8());
    sendArgument(key.toUtf8());
    sendArgument(value);
}

void GpgNetClient::sendAiOption(QString name, QString key, int value)
{
    sendCommand("AIOption", 3);
    sendArgument(name.toUtf8());
    sendArgument(key.toUtf8());
    sendArgument(value);
}

void GpgNetClient::sendClearSlot(int slot)
{
    sendCommand("ClearSlot", 1);
    sendArgument(slot);
}

void GpgNetClient::sendGameEnded()
{
    sendCommand("GameEnded", 0);
}

void GpgNetClient::sendGameResult(int army, int score)
{
    sendCommand("GameResult", 2);
    sendArgument(army);
    if (score > 0)
    {
        sendArgument("VICTORY 1");
    }
    else if (score == 0)
    {
        sendArgument("DRAW 0");
    }
    else
    {
        sendArgument("DEFEAT -1");
    }
}
