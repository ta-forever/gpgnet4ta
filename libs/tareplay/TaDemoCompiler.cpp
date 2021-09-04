#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qdir.h>
#include <QtCore/qobject.h>
#include <QtCore/qpair.h>
#include <QtCore/qmap.h>
#include <QtNetwork/qhostaddress.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>

#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>

#include "gpgnet/GpgNetParse.h"
#include "taflib/HexDump.h"
#include "taflib/Logger.h"
#include "tapacket/TADemoWriter.h"
#include "TaDemoCompilerMessages.h"
#include "TaReplayServer.h"
#include "TaDemoCompiler.h"

using namespace tareplay;

TaDemoCompiler::UserContext::UserContext(QTcpSocket* socket):
    gameId(0u),
    playerDpId(0u),
    gamePlayerNumber(0),
    dataStream(new QDataStream(socket))
{
    dataStream->setByteOrder(QDataStream::ByteOrder::LittleEndian);
}

TaDemoCompiler::GameContext::GameContext() :
    gameId(0u),
    expiryCountdown(GAME_EXPIRY_TICKS)
{ }

TaDemoCompiler::TaDemoCompiler(QString demoPathTemplate, QHostAddress addr, quint16 port):
    m_demoPathTemplate(demoPathTemplate)
{
    qInfo() << "[TaDemoCompiler::TaDemoCompiler] starting server on addr" << addr << "port" << port;
    m_tcpServer.listen(addr, port);
    if (!m_tcpServer.isListening())
    {
        qWarning() << "[TaDemoCompiler::TaDemoCompiler] server is not listening!";
    }
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &TaDemoCompiler::onNewConnection);

    startTimer(1000);
}

TaDemoCompiler::~TaDemoCompiler()
{
    m_tcpServer.close();
    for (const auto &socket : m_players.keys())
    {
        socket->close();
    }
}

void TaDemoCompiler::onNewConnection()
{
    try
    {
        QTcpSocket* socket = m_tcpServer.nextPendingConnection();
        qInfo() << "accepted connection from" << socket->peerAddress() << "port" << socket->peerPort();
        QObject::connect(socket, &QTcpSocket::readyRead, this, &TaDemoCompiler::onReadyRead);
        QObject::connect(socket, &QTcpSocket::stateChanged, this, &TaDemoCompiler::onSocketStateChanged);
        m_players[socket].reset(new UserContext(socket));
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaDemoCompiler::onNewConnection] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaDemoCompiler::onNewConnection] general exception:";
    }
}

void TaDemoCompiler::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            qInfo() << "[TaDemoCompiler::onSocketStateChanged] peer disconnected" << sender->peerAddress() << "port" << sender->peerPort();
            m_players.remove(sender);
            sender->deleteLater();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaDemoCompiler::onSocketStateChanged] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaDemoCompiler::onSocketStateChanged] general exception:";
    }
}

void TaDemoCompiler::onReadyRead()
{
    try
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        auto itUserContext = m_players.find(sender);
        if (itUserContext == m_players.end())
        {
            throw std::runtime_error("received data from unknown user!");
        }
        if (!itUserContext.value()->dataStream)
        {
            throw std::runtime_error("null datastream!");;
        }
        UserContext& userContext = *itUserContext.value().data();

        bool committedMoves = false;
        while (!itUserContext.value()->dataStream->atEnd())
        {
            QVariantList command = userContext.gpgNetParser.GetCommand(*userContext.dataStream);
            QString cmd = command[0].toString();

            if (cmd == "Move")
            {
                auto itGame = m_games.find(userContext.gameId);
                if (itGame != m_games.end())
                {
                    auto itGamePlayer = itGame->players.find(userContext.playerDpId);
                    if (itGamePlayer != itGame->players.end())
                    {
                        itGame->expiryCountdown = GAME_EXPIRY_TICKS;
                        if (itGame->demoCompilation)
                        {
                            commitMove(itGame.value(), userContext.gamePlayerNumber, GameMoveMessage(command));
                            committedMoves = true;
                        }
                    }
                }
            }
            else if (cmd == HelloMessage::ID)
            {
                HelloMessage msg(command);
                qInfo() << "[TaDemoCompiler::onReadyRead] received Hello from player" << msg.playerDpId << "in game" << msg.gameId;
                userContext.gameId = msg.gameId;
                userContext.playerDpId = msg.playerDpId;
                m_games[msg.gameId].players[msg.playerDpId] = m_players[sender];
            }
            else if (cmd == GameInfoMessage::ID)
            {
                GameContext& game = m_games[userContext.gameId];
                game.gameId = userContext.gameId;
                game.expiryCountdown = GAME_EXPIRY_TICKS;
                game.header = GameInfoMessage(command);
            }
            else if (cmd == GamePlayerMessage::ID)
            {
                auto itGame = m_games.find(userContext.gameId);
                if (itGame != m_games.end())
                {
                    GameContext& game = itGame.value();
                    game.expiryCountdown = GAME_EXPIRY_TICKS;
                    game.players[userContext.playerDpId]->gamePlayerInfo = GamePlayerMessage(command);
                }
            }
            else if (cmd == GameUnitDataMessage::ID)
            {
                auto itGame = m_games.find(userContext.gameId);
                if (itGame != m_games.end())
                {
                    GameUnitDataMessage msg(command);
                    tapacket::TUnitData ud(tapacket::bytestring((std::uint8_t*)msg.unitData.data(), msg.unitData.size()));
                    GameContext& game = itGame.value();
                    game.expiryCountdown = GAME_EXPIRY_TICKS;
                    if (ud.sub == 2 || ud.sub == 3 || ud.sub == 9)
                    {
                        game.unitData[QPair<quint8, quint32>(ud.sub, ud.id)] = msg.unitData;
                    }
                    else if (ud.sub == 0)
                    {
                        qInfo() << "[TaDemoCompiler::onReadyRead] player" << userContext.playerDpId << "reset unit data for game" << game.gameId;
                        game.unitData.clear();
                    }
                }
            }
            else if (cmd == GamePlayerLoading::ID)
            {
                GamePlayerLoading msg(command);
                std::ostringstream ss;
                ss << "[TaDemoCompiler::onReadyRead] player " << userContext.playerDpId << " is loading. locked in player ids: ";
                for (quint32 dpid : msg.lockedInPlayers)
                {
                    ss << dpid << ' ';
                }
                qInfo() << ss.str().c_str();
                auto itGame = m_games.find(userContext.gameId);
                if (itGame != m_games.end())
                {
                    if (!itGame->demoCompilation)
                    {
                        itGame->playersLockedIn = msg.lockedInPlayers;
                        itGame->finalFileName = m_demoPathTemplate.arg(itGame->gameId);
                        itGame->tempFileName = itGame->finalFileName + ".part";
                        itGame->demoCompilation = commitHeaders(itGame.value(), itGame->tempFileName);
                    }
                }
            }
            else
            {
                qWarning() << "[TaDemoCompiler::onReadyRead] unrecognised command" << cmd;
            }
        }
        if (committedMoves)
        {
            m_games[userContext.gameId].demoCompilation->flush();
        }
    }
    catch (const gpgnet::GpgNetParse::DataNotReady &)
    {
        qInfo() << "[TaDemoCompiler::onReadyRead] waiting for more data";
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaDemoCompiler::onReadyRead] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaDemoCompiler::onReadyRead] general exception:";
    }
}

std::shared_ptr<std::ostream> TaDemoCompiler::commitHeaders(const GameContext& game, QString filename)
{
    qInfo() << "[TaDemoCompiler::onReadyRead] creating new demo compilation" << filename;

    for (int numPlayer = 0; numPlayer < game.playersLockedIn.size(); ++numPlayer)
    {
        quint32 dpid = game.playersLockedIn[numPlayer];
        if (game.players.contains(dpid) && !game.players[dpid].isNull())
        {
            game.players[dpid]->gamePlayerNumber = numPlayer + 1;
        }
        else
        {
            qWarning() << "[TaDemoCompiler::onReadyRead] player" << dpid << "not known in game" << game.gameId;
        }
    }

    std::shared_ptr<std::ostream> fs(new std::ofstream(filename.toStdString(), std::ios::binary));
    tapacket::TADemoWriter tad(fs.get());

    tapacket::Header header;
    std::strcpy(header.magic, "TA Demo");
    header.version = 5u;
    header.numPlayers = game.playersLockedIn.size();
    header.maxUnits = game.header.maxUnits;
    header.mapName = game.header.mapName.toStdString();
    tad.write(header);

    tapacket::ExtraHeader extraHeader;
    extraHeader.numSectors = 0u;
    tad.write(extraHeader);

    for (quint32 dpid : game.playersLockedIn)
    {
        if (game.players.contains(dpid) && game.players[dpid])
        {
            UserContext &userContext = *game.players[dpid];
            tapacket::Player demoPlayer;
            demoPlayer.number = demoPlayer.color = userContext.gamePlayerNumber;
            demoPlayer.name = userContext.gamePlayerInfo.name.toStdString();
            demoPlayer.side = userContext.gamePlayerInfo.side;
            tad.write(demoPlayer);
        }
    }

    for (quint32 dpid : game.playersLockedIn)
    {
        if (game.players.contains(dpid) && game.players[dpid])
        {
            tapacket::PlayerStatusMessage demoPlayer;
            demoPlayer.number = game.players[dpid]->gamePlayerNumber;
            const QByteArray& playerStatus = game.players[dpid]->gamePlayerInfo.statusMessage;
            demoPlayer.statusMessage = tapacket::bytestring((std::uint8_t*)playerStatus.data(), playerStatus.size());
            demoPlayer.statusMessage = tapacket::TPacket::trivialSmartpak(demoPlayer.statusMessage, 0xffffffff);
            demoPlayer.statusMessage = tapacket::TPacket::compress(demoPlayer.statusMessage);
            tapacket::TPacket::encrypt(demoPlayer.statusMessage);
            tad.write(demoPlayer);
        }
    }

    tapacket::UnitData unitData;
    for (const auto& ud : game.unitData)
    {
        unitData.unitData += tapacket::bytestring((std::uint8_t*)ud.data(), ud.size());
    }
    tad.write(unitData);
    tad.flush();

    return fs;
}

void TaDemoCompiler::commitMove(const TaDemoCompiler::GameContext& game, int playerNumber, const GameMoveMessage& moves)
{
    // TADR file format requires:
    // - unencrypted
    // - compressed (0x04) or uncomprossed (0x03)
    // - 0 bytes no checksum (normally 2 bytes)
    // - 0 bytes no timestamp (normally 4 bytes)

    tapacket::TADemoWriter tad(game.demoCompilation.get());
    tapacket::Packet packet;
    packet.time = 0u;  // milliseconds since last packet, but only used by ancient versions of replayer
    packet.sender = playerNumber;
    packet.data.assign((std::uint8_t*)moves.moves.data(), moves.moves.size());
    tad.write(packet);
}

void TaDemoCompiler::timerEvent(QTimerEvent* event)
{
    try
    {
        closeExpiredGames();
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaDemoCompiler::timerEvent] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaDemoCompiler::timerEvent] general exception:";
    }
}

void TaDemoCompiler::closeExpiredGames()
{
    QList<quint32> expiredGameIds;
    for (auto& game : m_games)
    {
        if (--game.expiryCountdown <= 0)
        {
            expiredGameIds.append(game.gameId);
        }
    }

    for (quint32 gameid : expiredGameIds)
    {
        if (m_games[gameid].demoCompilation)
        {
            qInfo() << "[TaDemoCompiler::closeExpiredGames] game" << gameid << "has expired. closing" << m_games[gameid].finalFileName;
            m_games[gameid].demoCompilation.reset();
            QFile::rename(m_games[gameid].tempFileName, m_games[gameid].finalFileName);
        }
        m_games.remove(gameid);
    }
}
