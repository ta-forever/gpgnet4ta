#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qdatetime.h>
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

static const char* VERSION = "taf-0.14.2";

TaDemoCompiler::UserContext::UserContext(QTcpSocket* socket):
    gameId(0u),
    playerDpId(0u),
    gamePlayerNumber(0),
    dataStream(new QDataStream(socket))
{
    dataStream->setByteOrder(QDataStream::ByteOrder::LittleEndian);
    gpgNetSerialiser.reset(new gpgnet::GpgNetSend(*dataStream));
}

TaDemoCompiler::GameContext::GameContext() :
    gameId(0u),
    expiryCountdown(GAME_EXPIRY_TICKS)
{ }

TaDemoCompiler::TaDemoCompiler(QString demoPathTemplate, QHostAddress addr, quint16 port, quint32 minDemoSize):
    m_demoPathTemplate(demoPathTemplate),
    m_minDemoSize(minDemoSize)
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
        while (m_tcpServer.hasPendingConnections())
        {
            QTcpSocket* socket = m_tcpServer.nextPendingConnection();
            qInfo() << "[TaDemoCompiler::onNewConnection] accepted connection from" << socket->peerAddress() << "port" << socket->peerPort();
            QObject::connect(socket, &QTcpSocket::readyRead, this, &TaDemoCompiler::onReadyRead);
            QObject::connect(socket, &QTcpSocket::stateChanged, this, &TaDemoCompiler::onSocketStateChanged);
            m_players[socket].reset(new UserContext(socket));
        }
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
            sender->disconnect();
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
    QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
    try
    {
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

            if (cmd == GameMoveMessage::ID)
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
                qInfo() << "[TaDemoCompiler::onReadyRead] received Hello from player" << msg.playerPublicAddr << '/' << msg.playerDpId << "in game" << msg.gameId;
                userContext.gameId = msg.gameId;
                userContext.playerDpId = msg.playerDpId;
                userContext.playerPublicAddr = msg.playerPublicAddr;
                m_games[msg.gameId].players[msg.playerDpId] = m_players[sender];
            }
            else if (cmd == ReconnectMessage::ID)
            {
                ReconnectMessage msg(command);
                if (m_games.contains(msg.gameId) && m_games[msg.gameId].players.contains(msg.playerDpId))
                {
                    qInfo() << "[TaDemoCompiler::onReadyRead] received Reconnect from dplayId" << msg.playerDpId << "in game" << msg.gameId;
                    QSharedPointer<UserContext> oldContext = m_games[msg.gameId].players[msg.playerDpId];
                    userContext.gameId = msg.gameId;
                    userContext.playerDpId = msg.playerDpId;
                    userContext.playerPublicAddr = oldContext->playerPublicAddr;
                    userContext.gamePlayerInfo = oldContext->gamePlayerInfo;
                    userContext.gamePlayerNumber = oldContext->gamePlayerNumber;
                    m_games[msg.gameId].players[msg.playerDpId] = m_players[sender];
                }
                else
                {
                    qWarning() << "[TaDemoCompiler::onReadyRead] received Reconnect from unknown player dplayId" << msg.playerDpId << "in game" << msg.gameId;
                }
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
                        qInfo() << QString("[TaDemoCompiler::onReadyRead][GameUnitDataMessage] gameId:%1, dpid:%2, ipaddr:%3, pktid:%4, sub:%5, fill:%6, id:%7, status:%8, limit:%9, crc:%10, raw:%11")
                            .arg(game.gameId)
                            .arg(userContext.playerDpId, 10, 10)
                            .arg(userContext.playerPublicAddr, 15)
                            .arg(int(ud.pktid), 2, 16, QChar('0'))
                            .arg(ud.sub, 1, 16)
                            .arg(ud.fill, 0, 16)
                            .arg(ud.id, 8, 16, QChar('0'))
                            .arg(ud.u.statusAndLimit[0], 4, 16, QChar('0'))
                            .arg(ud.u.statusAndLimit[1], 4, 16, QChar('0'))
                            .arg(ud.u.crc, 8, 16, QChar('0'))
                            .arg(QString(msg.unitData.toHex()));
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
                    if (!itGame->demoCompilation)
                    {
                        //sendStopRecordingToAllInGame(itGame->gameId);
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
    { }
    catch (const std::exception & e)
    {
        qWarning() << "[TaDemoCompiler::onReadyRead] exception:" << e.what();
        if (sender)
        {
            qWarning() << "[TaDemoCompiler::onReadyRead] closing users connection ...";
            sender->close();
        }
    }
    catch (...)
    {
        qWarning() << "[TaDemoCompiler::onReadyRead] general exception:";
    }
}

std::shared_ptr<std::ostream> TaDemoCompiler::commitHeaders(const GameContext& game, QString filename)
{
    std::shared_ptr<std::ostream> fs;
    qInfo() << "[TaDemoCompiler::commitHeaders] creating new demo compilation" << filename;

    QVector<quint32> knownLockedInPlayers;
    for (quint32 dpid : game.playersLockedIn)
    {
        if (game.players.contains(dpid) && !game.players[dpid].isNull())
        {
            qInfo() << "[TaDemoCompiler::commitHeaders] known locked-in player:" << dpid;
            knownLockedInPlayers.push_back(dpid);
        }
        else
        {
            qInfo() << "[TaDemoCompiler::commitHeaders] unknown locked-in player" << dpid;
        }
    }

    if (knownLockedInPlayers.size() != game.playersLockedIn.size())
    {
        qWarning() << "[TaDemoCompiler::commitHeaders] cannot compile demo because some players failed to register";
        return fs;
    }

    for (int numPlayer = 0; numPlayer < knownLockedInPlayers.size(); ++numPlayer)
    {
        quint32 dpid = knownLockedInPlayers[numPlayer];
        if (game.players.contains(dpid) && !game.players[dpid].isNull())
        {
            game.players[dpid]->gamePlayerNumber = numPlayer + 1;
        }
        else
        {
            qWarning() << "[TaDemoCompiler::commitHeaders] player" << dpid << "not known in game" << game.gameId;
        }
    }

    fs.reset(new std::ofstream(filename.toStdString(), std::ios::binary));
    tapacket::TADemoWriter tad(fs.get());

    tapacket::Header header;
    std::strcpy(header.magic, "TA Demo");
    header.version = 5u;
    header.numPlayers = knownLockedInPlayers.size();
    header.maxUnits = game.header.maxUnits;
    header.mapName = game.header.mapName.toStdString();
    tad.write(header);

    tapacket::ExtraHeader extraHeader;
    extraHeader.numSectors = 2 + header.numPlayers;
    tad.write(extraHeader);
    {
        tapacket::ExtraSector sector;
        sector.sectorType = tapacket::ExtraSector::RECORDER_VERSION;
        sector.data.assign((std::uint8_t*)VERSION, std::strlen(VERSION));
        tad.write(sector);

        sector.sectorType = tapacket::ExtraSector::DATE;
        std::string dateString = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate).toStdString();
        sector.data.assign((std::uint8_t*)dateString.data(), dateString.size());
        tad.write(sector);

        for (quint32 dpid : knownLockedInPlayers)
        {
            sector.sectorType = tapacket::ExtraSector::PLAYER_ADDR;
            sector.data.clear();
            sector.data.append(0x50, 0);    // count, value
            std::string addr;
            if (game.players.contains(dpid) && game.players[dpid])
            {
                UserContext& userContext = *game.players[dpid];
                addr = userContext.playerPublicAddr.toStdString();
            }
            addr.append(1, 0);              // need a null terminator
            std::transform(addr.begin(), addr.end(), addr.begin(), [](std::uint8_t x) { return x ^ 42; });
            sector.data.append((std::uint8_t*)addr.data(), addr.size());
            tad.write(sector);
        }
    }

    for (quint32 dpid : knownLockedInPlayers)
    {
        if (game.players.contains(dpid) && game.players[dpid])
        {
            tapacket::Player demoPlayer;
            UserContext &userContext = *game.players[dpid];
            demoPlayer.number = demoPlayer.color = userContext.gamePlayerNumber;
            demoPlayer.name = userContext.gamePlayerInfo.name.toStdString();
            demoPlayer.side = userContext.gamePlayerInfo.side;
            tad.write(demoPlayer);
        }
    }

    for (quint32 dpid : knownLockedInPlayers)
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

    std::ofstream gameStartMarker(filename.toStdString() + ".meta");
    gameStartMarker << QDateTime::currentDateTimeUtc().toTime_t() << ' ' << fs->tellp();

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
            if (m_games[gameid].demoCompilation->tellp() >= m_minDemoSize)
            {
                qInfo() << "[TaDemoCompiler::closeExpiredGames] game" << gameid << "has expired. closing" << m_games[gameid].finalFileName;
                m_games[gameid].demoCompilation.reset();
                QFile::rename(m_games[gameid].tempFileName, m_games[gameid].finalFileName);
                QFile::remove(m_games[gameid].tempFileName + ".meta");
            }
            else
            {
                qInfo() << "[TaDemoCompiler::closeExpiredGames] game" << gameid << "has expired and is too small. Deleting" << m_games[gameid].finalFileName;
                m_games[gameid].demoCompilation.reset();
                QFile::remove(m_games[gameid].tempFileName);
                QFile::remove(m_games[gameid].tempFileName + ".meta");
            }
        }
        m_games.remove(gameid);
    }
}

void TaDemoCompiler::sendStopRecordingToAllInGame(quint32 gameId)
{
    if (m_games.contains(gameId))
    {
        for (auto userContext : m_games.value(gameId).players.values())
        {
            if (userContext && userContext->gpgNetSerialiser)
            {
                qInfo() << "[TaDemoCompiler::sendStopRecordingToAllInGame] gameId:" << gameId << "playerDpId:" << userContext->playerDpId << "ip:" << userContext->playerPublicAddr;
                userContext->gpgNetSerialiser->sendCommand(StopRecordingMessage::ID, 0);
            }
        }
    }
}
