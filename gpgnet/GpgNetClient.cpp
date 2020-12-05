#include "GpgNetClient.h"
#include "GpgNetParse.h"

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
    QHostAddress gpgnetHost("127.0.0.1");
    quint16 gpgnetPort = 0xdead;
    SplitHostAndPort(gpgnetHostAndPort, gpgnetHost, gpgnetPort);

    m_datastream.setByteOrder(QDataStream::LittleEndian);
    m_socket.connectToHost(gpgnetHost, gpgnetPort);
    if (!m_socket.waitForConnected(3000))
    {
        throw std::runtime_error("unable to connect to gpgnet");
    }
    qInfo() << "[GpgNetClient::GpgNetGameLauncher] connect" << m_socket.peerAddress().toString() << ':' << m_socket.peerPort();
    QObject::connect(&m_socket, &QTcpSocket::readyRead, this, &GpgNetClient::onReadyRead);
    QObject::connect(&m_socket, &QTcpSocket::stateChanged, this, &GpgNetClient::onSocketStateChanged);
}

void GpgNetClient::onReadyRead()
{
    QAbstractSocket* sender = static_cast<QAbstractSocket*>(QObject::sender());
    while (sender->bytesAvailable() > 0)
    {
        QVariantList serverCommand = gpgnet::GpgNetParse::GetCommand(m_datastream);
        QString cmd = serverCommand[0].toString();
        qInfo() << "[GpgNetClient::onReadyRead] gpgnet command received:" << cmd;

        if (cmd == "CreateLobby")
        {
            CreateLobbyCommand clc;
            clc.Set(serverCommand);
            m_gpgnetPlayerIds[clc.playerName] = clc.playerId;
            emit createLobby(
                clc.protocol, clc.localPort, clc.playerName,
                clc.playerId, clc.natTraversal);
        }
        else if (cmd == "HostGame")
        {
            HostGameCommand hgc;
            hgc.Set(serverCommand);
            emit hostGame(hgc.mapName);
        }
        else if (cmd == "JoinGame")
        {
            JoinGameCommand jgc(serverCommand);
            m_gpgnetPlayerIds[jgc.remotePlayerName()] = jgc.remotePlayerId;
            qInfo() << "[GpgNetClient::onReadyRead] join game: playername=" << jgc.remotePlayerName() << "playerId=" << jgc.remotePlayerId;
            emit joinGame(jgc.remoteHost(), jgc.remotePlayerName(), jgc.remotePlayerId);
        }
        else if (cmd == "ConnectToPeer")
        {
            ConnectToPeerCommand ctp(serverCommand);
            m_gpgnetPlayerIds[ctp.playerName()] = ctp.playerId;
            qInfo() << "[GpgNetClient::onReadyRead] connect to peer: playername=" << ctp.playerName() << "playerId=" << ctp.playerId;
            emit connectToPeer(ctp.host(), ctp.playerName(), ctp.playerId);
        }
        else if (cmd == "DisconnectFromPeer")
        {
            DisconnectFromPeerCommand ctp(serverCommand);
            qInfo() << "[GpgNetClient::onReadyRead] disconnect from peer: playerid=" << ctp.playerId;
            //gpgPlayerIds erase where value == ctp.playerId; // not super important
            emit disconnectFromPeer(ctp.playerId);
        }
    }
}

quint32 GpgNetClient::lookupPlayerId(QString playerName)
{
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
    if (socketState == QAbstractSocket::UnconnectedState)
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        qInfo() << "[GpgNetClient::onSocketStateChanged/UnconnectedState]" << sender->peerAddress().toString() << ":" << sender->peerPort();
    }
}