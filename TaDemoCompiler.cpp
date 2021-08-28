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
#include "tademo/TADemoWriter.h"
#include "tademo/HexDump.h"

#include "Logger.h"
#include "TaDemoCompilerMessages.h"
#include "TaReplayServer.h"

class TaDemoCompiler: public QObject
{
public:
    TaDemoCompiler(QString demoPathTemplate, QHostAddress addr, quint16 port);
    ~TaDemoCompiler();

private:

    struct UserContext
    {
        UserContext() { }
        UserContext(QTcpSocket* socket);

        quint32 gameId;
        quint32 playerDpId;
        QSharedPointer<QDataStream> dataStream;
        gpgnet::GpgNetParse gpgNetParser;
        GamePlayerMessage gamePlayerInfo;
        int gamePlayerNumber;   // 1..10
    };

    static const quint32 GAME_EXPIRY_TICKS = 60;
    struct GameContext
    {
        GameContext();

        quint32 gameId;
        GameInfoMessage header;
        QMap<quint32, QSharedPointer<UserContext> > players;    // keyed by Dplay ID
        QVector<quint32> playersLockedIn;                       // those that actually progressed to loading
        QMap<QPair<quint8, quint32>, QByteArray> unitData;      // keyed by sub,id

        std::shared_ptr<std::ostream> demoCompilation;
        int expiryCountdown;    // continuing messages from players keep this counter from expiring
    };

    void onNewConnection();
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);
    void onReadyRead();
    void closeExpiredGames();
    void timerEvent(QTimerEvent* event);

    std::shared_ptr<std::ostream> commitHeaders(const GameContext &);
    void commitMove(const GameContext &, int playerNumber, const GameMoveMessage &);

    QString m_demoPathTemplate;
    QTcpServer m_tcpServer;
    QMap<QTcpSocket*, QSharedPointer<UserContext> > m_players;
    QMap<quint32, GameContext> m_games;
};

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
            qWarning() << "[TaDemoCompiler::onReadyRead] received data from unknown user!";
            return;
        }
        if (!itUserContext.value()->dataStream)
        {
            qCritical() << "[TaDemoCompiler::onReadyRead] null datastream for player" << itUserContext.value()->playerDpId << "in game" << itUserContext.value()->gameId;
            return;
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
            else if (cmd == "Hello")
            {
                HelloMessage msg(command);
                qInfo() << "[TaDemoCompiler::onReadyRead] received Hello from player" << msg.playerDpId << "in game" << msg.gameId;
                userContext.gameId = msg.gameId;
                userContext.playerDpId = msg.playerDpId;
                m_games[msg.gameId].players[msg.playerDpId] = m_players[sender];
            }
            else if (cmd == "GameInfo")
            {
                GameContext& game = m_games[userContext.gameId];
                qInfo() << "[TaDemoCompiler::onReadyRead] player" << userContext.playerDpId << "forwarded header for game" << userContext.gameId;
                game.gameId = userContext.gameId;
                game.expiryCountdown = GAME_EXPIRY_TICKS;
                game.header = GameInfoMessage(command);
            }
            else if (cmd == "GamePlayer")
            {
                //qInfo() << "[TaDemoCompiler::onReadyRead] player" << itUserContext->playerDpId << "forwarded player info";
                auto itGame = m_games.find(userContext.gameId);
                if (itGame != m_games.end())
                {
                    GameContext& game = itGame.value();
                    qInfo() << "[TaDemoCompiler::onReadyRead] player" << userContext.playerDpId << "forwarded player info for game" << game.gameId;
                    game.expiryCountdown = GAME_EXPIRY_TICKS;
                    game.players[userContext.playerDpId]->gamePlayerInfo = GamePlayerMessage(command);
                }
            }
            else if (cmd == "UnitData")
            {
                //qInfo() << "[TaDemoCompiler::onReadyRead] player" << itUserContext->playerDpId << "forwarded unit data";
                auto itGame = m_games.find(userContext.gameId);
                if (itGame != m_games.end())
                {
                    //qInfo() << "[TaDemoCompiler::onReadyRead] player" << itUserContext->playerDpId << "forwarded unit data for game" << itGame->gameId;
                    GameUnitDataMessage msg(command);
                    TADemo::TUnitData ud(TADemo::bytestring((std::uint8_t*)msg.unitData.data(), msg.unitData.size()));
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
            else if (cmd == "GamePlayerLoading")
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
                        itGame->demoCompilation = commitHeaders(itGame.value());
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

std::shared_ptr<std::ostream> TaDemoCompiler::commitHeaders(const GameContext& game)
{
    QString filename = m_demoPathTemplate.arg(game.gameId);
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
    TADemo::TADemoWriter tad(fs.get());

    TADemo::Header header;
    std::strcpy(header.magic, "TA Demo");
    header.version = 5u;
    header.numPlayers = game.playersLockedIn.size();
    header.maxUnits = game.header.maxUnits;
    header.mapName = game.header.mapName.toStdString();
    tad.write(header);

    TADemo::ExtraHeader extraHeader;
    extraHeader.numSectors = 0u;
    tad.write(extraHeader);

    for (quint32 dpid : game.playersLockedIn)
    {
        if (game.players.contains(dpid) && game.players[dpid])
        {
            UserContext &userContext = *game.players[dpid];
            TADemo::Player demoPlayer;
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
            TADemo::PlayerStatusMessage demoPlayer;
            demoPlayer.number = game.players[dpid]->gamePlayerNumber;
            const QByteArray& playerStatus = game.players[dpid]->gamePlayerInfo.statusMessage;
            demoPlayer.statusMessage = TADemo::bytestring((std::uint8_t*)playerStatus.data(), playerStatus.size());
            demoPlayer.statusMessage = TADemo::TPacket::trivialSmartpak(demoPlayer.statusMessage, 0xffffffff);
            demoPlayer.statusMessage = TADemo::TPacket::compress(demoPlayer.statusMessage);
            TADemo::TPacket::encrypt(demoPlayer.statusMessage);
            tad.write(demoPlayer);
        }
    }

    TADemo::UnitData unitData;
    for (const auto& ud : game.unitData)
    {
        unitData.unitData += TADemo::bytestring((std::uint8_t*)ud.data(), ud.size());
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

    TADemo::TADemoWriter tad(game.demoCompilation.get());
    TADemo::Packet packet;
    packet.time = 0u;  // milliseconds since last packet, but only used by ancient versions of replayer
    packet.sender = playerNumber;
    packet.data.assign((std::uint8_t*)moves.moves.data(), moves.moves.size());
    tad.write(packet);
}

void TaDemoCompiler::timerEvent(QTimerEvent* event)
{
    closeExpiredGames();
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
        qInfo() << "[TaDemoCompiler::closeExpiredGames] game" << gameid << "has expired. closing";
        m_games.remove(gameid);
    }
}


int doMain(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("TaDemoCompiler");
    QCoreApplication::setApplicationVersion("0.14");

    QCommandLineParser parser;
    parser.setApplicationDescription("TA Demo Compiler");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs.", "logfile", "c:\\temp\\tademocompiler.log"));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug).", "logfile", "5"));
    parser.addOption(QCommandLineOption("demofile", "template for file path in which to save demo files.", "demopath", "taf-game-%1.tad"));
    parser.addOption(QCommandLineOption("addr", "interface address to listen for demo data.", "addr"));
    parser.addOption(QCommandLineOption("port", "port on which to listen for demo data.", "port"));
    parser.addOption(QCommandLineOption("livedelaysecs", "Number of seconds to delay the live replay by", "livedelaysecs", "300"));
    parser.addOption(QCommandLineOption("compilerenable", "run the TA Demo Compiler Server"));
    parser.addOption(QCommandLineOption("replayerenable", "run the TA Demo Replay Server"));
    parser.process(app);

    Logger::Initialise(parser.value("logfile").toStdString(), Logger::Verbosity(parser.value("loglevel").toInt()));
    qInstallMessageHandler(Logger::Log);

    std::shared_ptr<TaDemoCompiler> compiler;
    std::shared_ptr<TaReplayServer> replayServer;
    quint16 port = parser.value("port").toInt();
    if (parser.isSet("compilerenable"))
    {
        compiler.reset(new TaDemoCompiler(parser.value("demofile"), QHostAddress(parser.value("addr")), port++));
    }
    if (parser.isSet("replayerenable"))
    {
        replayServer.reset(new TaReplayServer(
            parser.value("demofile"),
            QHostAddress(parser.value("addr")),
            port++,
            parser.value("livedelaysecs").toUInt()));
    }

    app.exec();
    return 0;
}

int main(int argc, char* argv[])
{
    try
    {
        return doMain(argc, argv);
    }
    catch (std::exception & e)
    {
        std::cerr << "[main catch std::exception] " << e.what() << std::endl;
        qWarning() << "[main catch std::exception]" << e.what();
        return 1;
    }
    catch (...)
    {
        std::cerr << "[main catch ...] " << std::endl;
        qWarning() << "[main catch ...]";
        return 1;
    }
}
