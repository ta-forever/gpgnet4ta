#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qdir.h>
#include <QtCore/qtemporaryfile.h>
#include <QtCore/qprocess.h>

#include <iostream>
#include <sstream>

#include "gpgnet/GpgNetClient.h"
#include "tademo/TADemoParser.h"
#include "tafnet/TafnetGameNode.h"
#include "tademo/Watchdog.h"

#include "Logger.h"
#include "ConsoleReader.h"
#include "GpgNetGameLauncher.h"
#include "TaLobby.h"
#include "MessageBoxThread.h"

#ifdef _ENABLE_IRC
#include "IrcForward.h"
#else
#include "IrcForwardMock.h"
#endif

using namespace gpgnet;

#ifdef _WIN32
const char *MAP_TOOL_EXE = "maptool.exe";
#endif

#ifdef linux
const char *MAP_TOOL_EXE = "maptool";
#endif

class ForwardGameEventsToGpgNet : public GameEventHandlerQt
{
    gpgnet::GpgNetClient &m_gpgNetClient;
    bool m_isHost;
    std::function<QString(QString)> getMapDetails;

    // TA constructs AI's name by prepending with "AI:" and appending with a sequence number
    // but unfortunately character count is limited so the sequence number might be dropped
    // in that case multiple AI's don't have unique names
    // Here we overwrite the last character with the slot number to increase chance of uniqueness
    QString makeAiName(QString nominalName, quint8 slot)
    {
        if (!nominalName.isEmpty())
        {
            return nominalName.mid(0, nominalName.size() - 1) + QString::number(slot);
        }
        else
        {
            return QString() + "AI" + QString::number(slot);
        }
    }

public:
    ForwardGameEventsToGpgNet(gpgnet::GpgNetClient& gpgNetClient, std::function<QString(QString)> getMapDetails) :
        m_gpgNetClient(gpgNetClient),
        getMapDetails(getMapDetails)
    { }

    virtual void onGameSettings(QString mapName, quint16 maxUnits, QString hostName, QString localName)
    {
        try
        {
            TADemo::Watchdog wd("ForwardGameEventsToGpgNet::onGameSettings", 100);
            qInfo() << "[ForwardGameEventsToGpgNet::onGameSettings] GameOption" << mapName << "host" << hostName << "local" << localName;
            m_isHost = hostName == localName;
            if (m_isHost)
            {
                m_gpgNetClient.gameOption("MapDetails", getMapDetails(mapName));
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onGameSettings] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onGameSettings] unknown exception";
        }
    }

    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 slot, quint8 side, bool isWatcher, bool isAI, bool isDead, quint8 armyNumber, quint8 _teamNumber, QStringList mutualAllies)
    {
        try
        {
            TADemo::Watchdog wd("ForwardGameEventsToGpgNet::onPlayerStatus", 100);
            QString gpgnetId = QString::number(m_gpgNetClient.lookupPlayerId(name));

            // Forged Alliance reserves Team=1 for the team-not-selected team
            if (isAI || gpgnetId == 0)
            {
                int teamNumber = int(_teamNumber);
                QString aiName = makeAiName(name, slot);
                qInfo() << "[ForwardGameEventsToGpgNet::onPlayerStatus] aiName:" << aiName << "slot:" << slot << "army:" << armyNumber << "team:" << teamNumber << "side:" << side << "isDead:" << isDead << "isWatcher:" << isWatcher;
                if (m_isHost)
                {
                    m_gpgNetClient.aiOption(aiName, "Team", teamNumber);
                    m_gpgNetClient.aiOption(aiName, "StartSpot", slot);
                    m_gpgNetClient.aiOption(aiName, "Color", slot);
                    m_gpgNetClient.aiOption(aiName, "Army", armyNumber);
                    m_gpgNetClient.aiOption(aiName, "Faction", side);
                }
            }
            else
            {
                int teamNumber = isWatcher ? -1 : int(_teamNumber);
                qInfo() << "[ForwardGameEventsToGpgNet::onPlayerStatus] playerName:" << name << "id:" << gpgnetId << "slot:" << slot << "army:" << armyNumber << "team:" << teamNumber << "side:" << side << "isDead:" << isDead << "isWatcher:" << isWatcher;
                if (m_isHost)
                {
                    m_gpgNetClient.playerOption(gpgnetId, "Team", teamNumber);
                    m_gpgNetClient.playerOption(gpgnetId, "StartSpot", slot);
                    m_gpgNetClient.playerOption(gpgnetId, "Color", slot);
                    m_gpgNetClient.playerOption(gpgnetId, "Army", armyNumber);
                    m_gpgNetClient.playerOption(gpgnetId, "Faction", side);
                }
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onPlayerStatus] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onPlayerStatus] unknown exception";
        }
    }

    virtual void onClearSlot(quint32 dplayId, QString name, quint8 slot)
    {
        try
        {
            TADemo::Watchdog wd("ForwardGameEventsToGpgNet::onClearSlot", 100);
            qInfo() << "[ForwardGameEventsToGpgNet::onClearSlot]" << name << "slot" << slot;
            if (m_isHost)
            {
                m_gpgNetClient.clearSlot(slot);
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onClearSlot] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onClearSlot] unknown exception";
        }
    }

    virtual void onGameStarted(quint32 tick, bool teamsFrozen)
    {
        try
        {
            TADemo::Watchdog wd("ForwardGameEventsToGpgNet::onGameStarted", 100);
            if (!teamsFrozen)
            {
                qInfo() << "[ForwardGameEventsToGpgNet::onGameStarted] GameState 'Launching'";
                if (m_isHost)
                {
                    m_gpgNetClient.gameState("Launching", "Launching");
                }
            }
            else
            {
                qInfo() << "[ForwardGameEventsToGpgNet::onGameStarted] GameState 'Live'";
                if (m_isHost)
                {
                    m_gpgNetClient.gameState("Launching", "Live");
                }
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onGameStarted] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onGameStarted] unknown exception";
        }
    }

    virtual void onGameEnded(QList<QVariantMap> results)
    {
        try
        {
            TADemo::Watchdog wd("ForwardGameEventsToGpgNet::onGameEnded", 100);
            for (const QVariantMap& result : results)
            {
                int army = result.value("army").toInt();
                int score = result.value("score").toInt();
                qInfo() << "[ForwardGameEventsToGpgNet::onGameEnded]" << result;
                m_gpgNetClient.gameResult(army, score);
            }
            qInfo() << "[ForwardGameEventsToGpgNet::onGameEnded] GameState Ended";
            m_gpgNetClient.gameState("Ended", "Ended");
        }
        catch (std::exception &e)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onGameEnded] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[ForwardGameEventsToGpgNet::onGameEnded] unknown exception";
        }
    }

    virtual void onChat(QString msg, bool isLocalPlayerSource)
    { }
};


class HandleGameStatus : public GameEventHandlerQt
{
    IrcForward *m_irc;
    QString m_channel;
    TaLobby& m_taLobby;
    int m_quitRequested;
    QString m_selectedMap;

    // fixed size 10 slots
    QVector<QString> m_playerNames;
    QVector<bool> m_isAI;
    QVector<bool> m_isWatcher;

public:
    HandleGameStatus(IrcForward* irc, QString channel, TaLobby& taLobby) :
        m_irc(irc),
        m_channel(channel),
        m_taLobby(taLobby),
        m_quitRequested(0),
        m_playerNames(10u),
        m_isAI(10u, false),
        m_isWatcher(10u, false)
    { }

    virtual void onGameSettings(QString mapName, quint16 maxUnits, QString hostName, QString localName)
    {
        try
        {
            TADemo::Watchdog wd("HandleGameStatus::onGameSettings", 100);
            if (mapName != m_selectedMap)
            {
                doSend("Map changed to: " + mapName.toStdString(), true, false);
                m_selectedMap = mapName;
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[HandleGameStatus::onGameSettings] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[HandleGameStatus::onGameSettings] unknown exception";
        }
    }

    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 slot, quint8 side, bool isWatcher, bool isAI, bool isDead, quint8 armyNumber, quint8 _teamNumber, QStringList mutualAllies)
    {
        try
        {
            if (slot < m_playerNames.size())
            {
                m_playerNames[slot] = name;
                m_isAI[slot] = isAI;
                m_isWatcher[slot] = isWatcher;
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[HandleGameStatus::onPlayerStatus] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[HandleGameStatus::onPlayerStatus] unknown exception";
        }
    }

    virtual void onClearSlot(quint32 dplayId, QString name, quint8 slot)
    {
        try
        {
            TADemo::Watchdog wd("HandleGameStatus::onClearSlot", 100);
            if (slot < m_playerNames.size())
            {
                m_playerNames[slot].clear();
                m_isAI[slot] = false;
                m_isWatcher[slot] = false;
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[HandleGameStatus::onClearSlot] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[HandleGameStatus::onClearSlot] unknown exception";
        }
    }

    virtual void onGameStarted(quint32 tick, bool teamsFrozen)
    {
        try
        {
            TADemo::Watchdog wd("HandleGameStatus::onGameStarted", 100);
            const int occupancyCount = getOccupancyCount();
            const int aiCount = getAiCount();
            const int watcherCount = getWatcherCount();
            const int humanPlayerCount = occupancyCount - aiCount - watcherCount;

            if (m_irc && m_irc->isActive()) {
                m_irc->quit(m_irc->realName());
                m_irc->close();
            }

            if (!teamsFrozen && aiCount == 0)
            {
                if (humanPlayerCount >= 3)
                {
                    doSend("Game becomes rated after 1:00. Finalise teams by then", false, true);
                    doSend("Self-d/alt-f4 before to rescind, or ally afterwards", false, true);
                    doSend("/quit to disconnect so team can .take (unless UR host!)", false, true);
                }
                else if (humanPlayerCount == 2)
                {
                    doSend("Game becomes rated after 1:00", false, true);
                    doSend("Self-d/alt-f4 before to rescind, or ally afterwards", false, true);
                }

                if (m_irc && m_irc->isActive())
                {
                    doSend("/closeirc to close your ingame/TAF chat relay", false, true);
                }
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[HandleGameStatus::onGameStarted] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[HandleGameStatus::onGameStarted] unknown exception";
        }
    }

    virtual void onGameEnded(QList<QVariantMap> results)
    {
        try
        {
            TADemo::Watchdog wd("HandleGameStatus::onGameEnded", 100);
            doSend("Game over man, game over!", true, true);
            for (const QVariantMap &result : results)
            {
                std::ostringstream ss;
                std::string alias = result.value("alias").toString().toStdString();
                std::string realName = result.value("realName").toString().toStdString();
                int slot = result.value("slot").toInt();
                int team = result.value("team").toInt();
                int score = result.value("score").toInt();
                std::string outcome;
                if (score > 0)
                    outcome = "VICTORY";
                else if (score == 0)
                    outcome = "DRAW";
                else
                    outcome = "DEFEAT";

                if (realName.empty() || realName == alias)
                {
                    ss << alias << ": " << outcome;
                }
                else
                {
                    ss << alias << " (aka " << realName << "): " << outcome;
                }
                doSend(ss.str(), true, true);
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[HandleGameStatus::onGameEnded] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[HandleGameStatus::onGameEnded] unknown exception";
        }
    }

    virtual void onChat(QString msg, bool isLocalPlayerSource)
    {
        try
        {
            TADemo::Watchdog wd("HandleGameStatus::onChat", 100);
            if (isLocalPlayerSource && msg.size() > 0)
            {
                int n = msg.lastIndexOf("> ");
                if (msg[0] == '<' && n >= 0 && n + 2 < msg.size())
                {
                    // remove the <name> tag so player doesn't get pinged in IRC
                    msg = msg.mid(n + 2);
                }

                doSend(msg.toStdString(), true, false);

                m_quitRequested = msg == "/quit" ? 1+m_quitRequested : 0;
                m_quitRequested = msg == "-harald" ? 2+m_quitRequested : 0;
                if (m_quitRequested == 1)
                {
                    doSend("Type /quit again to disconnect your game", false, true);
                }
                else if (m_quitRequested >= 2)
                {
                    doSend("ok your game is disconnected you can alt-f4", false, true);
                    qInfo() << "[HandleGameStatus::onChat] terminating with m_quitRequested=" << m_quitRequested;
                    qApp->quit();
                }
                else if (msg == "/closeirc")
                {
                    if (m_irc && m_irc->isActive()) {
                        m_irc->quit(m_irc->realName());
                        m_irc->close();
                        doSend("ok IRC relay is closed", false, true);
                    }
                }
            }
        }
        catch (std::exception &e)
        {
            qWarning() << "[HandleGameStatus::onChat] exception" << e.what();
        }
        catch (...)
        {
            qWarning() << "[HandleGameStatus::onChat] unknown exception";
        }
    }

private:
    void doSend(const std::string & chat, bool toIrc, bool toGame)
    {
        if (m_irc && m_irc->isActive() && toIrc)
        {
            m_irc->sendCommand(IrcCommand::createMessage(m_channel, chat.c_str()));
        }
        if (toGame)
        {
            m_taLobby.echoToGame(false, "", QString::fromStdString(chat));
        }
    }

    int getOccupancyCount()
    {
        return std::accumulate(m_playerNames.begin(), m_playerNames.end(), 0u,
            [](int a, QString name) { return name.isEmpty() ? a : a + 1; });
    }

    int getAiCount()
    {
        return std::accumulate(m_isAI.begin(), m_isAI.end(), 0u,
            [](int a, bool isAI) { return isAI ? a + 1 : a; });
    }

    int getWatcherCount()
    {
        return std::accumulate(m_isWatcher.begin(), m_isWatcher.end(), 0u,
            [](int a, bool isWatcher) { return isWatcher ? a + 1 : a; });

    }
};


void SplitUserHostPortChannel(QString url, QString &user, QString &host, quint16 &port, QString &resource)
{
    // user@host:port/resource
    QStringList temp = url.split("@");
    if (temp.size()>1)
    {
        user = temp[0];
        url = temp[1];
    }

    temp = url.split("/");
    if (temp.size() > 1)
    {
        url = temp[0];
        resource = temp[1];
    }

    temp = url.split(":");
    if (temp.size() > 1)
    {
        host = temp[0];
        port = temp[1].toInt();
    }
    else
    {
        host = url;
    }
}


QString quote(QString text)
{
    return '"' + text + '"';
}

QString getMapDetails(QString gamePath, QString maptoolExePath, QString _mapName)
{
    qInfo() << "[getMapDetails]" << gamePath << maptoolExePath << _mapName;
    QStringList args = { "--gamepath", gamePath, "--mapname", _mapName+'$', "--hash" };

    qInfo() << "[getMapDetails] exe:" << maptoolExePath << ", args:" << args;
    QProcess process;
    process.start(maptoolExePath, args);
    process.waitForFinished(3000);

    QString err = process.readAllStandardError();
    if (!err.isEmpty())
    {
      qWarning() << err;
    }

    QString result = process.readAllStandardOutput();
    qInfo() << "[getMapDetails]" << result;

    return result;
}

int doMain(int argc, char* argv[])
{
    const char* DEFAULT_DPLAY_REGISTERED_GAME_GUID = "{1336f32e-d116-4633-b853-4fee1ec91ea5}";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_PATH = "c:\\cavedog\\totala";
    const char *DEFAULT_GAME_INI_TEMPLATE = "taforever.ini.template";
    const char* DEFAULT_GAME_INI = "TAForever.ini";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_MOD = "TACC";

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("GPGNet4TA");
    QCoreApplication::setApplicationVersion("0.13.3");
    //app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription("GPGNet facade for Direct Play games");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("autolaunch", "Normally gpgnet4ta sets up the connections then waits for a /launch command before it launches TA. This option causes TA to launch straight away."));
    parser.addOption(QCommandLineOption("country", "Player country code.", "code"));
    parser.addOption(QCommandLineOption("deviation", "Player rating deviation.", "deviation"));
    parser.addOption(QCommandLineOption("gamemod", "Name of the game variant (used to generate a DirectPlay registration that doesn't conflict with another variant.", "gamemod", DEFAULT_DPLAY_REGISTERED_GAME_MOD));
    parser.addOption(QCommandLineOption("gamepath", "Path from which to launch game. (required for --registerdplay).", "path", DEFAULT_DPLAY_REGISTERED_GAME_PATH));
    parser.addOption(QCommandLineOption("gpgnet", "Uri to GPGNet.", "host:port"));
    parser.addOption(QCommandLineOption("irc", "user@host:port/channel for the ingame irc channel to join.", "irc"));
    parser.addOption(QCommandLineOption("lobbybindaddress", "Interface on which to bind the lobby interface.", "lobbybindaddress", "127.0.0.1"));
    parser.addOption(QCommandLineOption("lockoptions", "Lock (some of) the lobby options."));
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs.", "logfile", "c:\\temp\\gpgnet4ta.log"));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug).", "logfile", "5"));
    parser.addOption(QCommandLineOption("mean", "Player rating mean.", "mean"));
    parser.addOption(QCommandLineOption("numgames", "Player game count.", "count"));
    parser.addOption(QCommandLineOption("players", "Max number of players 2 to 10.", "players", "10"));
    parser.addOption(QCommandLineOption("proactiveresend", "Measure packet-loss during game setup and thereafter send multiple copies of packets accordingly."));
    parser.addOption(QCommandLineOption("launchserverport", "Specifies port that LaunchServer is listening on", "launchserverport", "48684"));
    parser.addOption(QCommandLineOption("consoleport", "Specifies port for ConsoleReader to listen on (consoleport receives less-privileged commands than LaunchServer does)", "48685"));
    parser.process(app);

    Logger::Initialise(parser.value("logfile").toStdString(), Logger::Verbosity(parser.value("loglevel").toInt()));
    qInstallMessageHandler(Logger::Log);

    if (!QFileInfo(parser.value("gamepath")).isDir())
    {
        qCritical() << QString("Unable to find game path ") + parser.value("gamepath");
        return 1;
    }

    QString dplayGuid = QUuid::createUuidV5(QUuid(DEFAULT_DPLAY_REGISTERED_GAME_GUID), parser.value("gamemod").toUpper()).toString();
    QString dplayAppName = "Total Annihilation Forever (" + parser.value("gamemod").toUpper() + ")";

    std::shared_ptr<IrcForward> ircForward;
    QString ircChannel("#BILLYIDOL[ingame]");
    if (parser.isSet("irc"))
    {
        QString user("BILLYIDOL[ingame]");
        QString host("irc.taforever.com");
        quint16 port(6667);

        SplitUserHostPortChannel(parser.value("irc"), user, host, port, ircChannel);
        ircForward.reset(new IrcForward);
        ircForward->setHost(host);
        ircForward->setUserName(user);
        ircForward->setNickName(user);
        ircForward->setRealName(user);
        ircForward->setPort(port);
    }

    if (parser.isSet("gpgnet"))
    {
        QString gamePath = parser.value("gamepath");

        // GpgNetClient receives from gpgnet instructions to host/join/connect to peers; GpgNetClient sends back to gpgnet game and player status
        gpgnet::GpgNetClient gpgNetClient(parser.value("gpgnet"));

        // LaunchClient connects the a LaunchServer (typically instantiated by the TALauncer app)
        LaunchClient launchClient(QHostAddress("127.0.0.1"), parser.value("launchserverport").toInt());

        // GpgNetGameLauncher interprets instructions from GpgNetClient to launch TA as a host or as a joiner
        GpgNetGameLauncher launcher(
            DEFAULT_GAME_INI_TEMPLATE,
            gamePath,
            gamePath + "/" + DEFAULT_GAME_INI,
            dplayGuid,
            parser.value("players").toInt(),
            parser.isSet("lockoptions"),
            1000,
            launchClient,
            gpgNetClient);

        // TaLobby is a conglomerate of objects that handles a man-in-the-middle relay of TA network traffic
        // Together they work to tunnel everything through a single UDP port
        // That UDP port is expected to be one brokered by the FAF ICE adapter independently of gpgnet4ta
        // TaLobby needs to be told explicetly to whom connections are to be made and on which UDP ports peers can be found
        // (viz all the Qt signal connections from GpgNetClient to TaLobby)
        TaLobby lobby(QUuid(dplayGuid), "127.0.0.1", "127.0.0.1", "127.0.0.1", parser.isSet("proactiveresend"));
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::createLobby, &lobby, &TaLobby::onCreateLobby);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::joinGame, &lobby, &TaLobby::onJoinGame);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::connectToPeer, &lobby, &TaLobby::onConnectToPeer);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::disconnectFromPeer, &lobby, &TaLobby::onDisconnectFromPeer);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::createLobby, &launcher, &GpgNetGameLauncher::onCreateLobby);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::hostGame, [&launcher, &parser](QString mapName) {
            launcher.onHostGame(mapName, getMapDetails(parser.value("gamepath"), MAP_TOOL_EXE, mapName));
            if (parser.isSet("autolaunch")) launcher.onLaunchGame();
        });
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::joinGame, [&launcher, &parser](QString host, QString playerName, QString, int playerId) {
            launcher.onJoinGame(host, playerName, playerName, playerId);
            if (parser.isSet("autolaunch")) launcher.onLaunchGame();
        });
        QObject::connect(&launcher, &GpgNetGameLauncher::gameTerminated, &app, &QCoreApplication::quit);

        // The TaLobby also takes the opporunity to snoop the network traffic that it handles in order to work out whats happening in the game
        // (eg game started/ended state; selected map, players and teams at start of game; and winners/losers/draws at end of game)
        // This information is passed on to the GpgNetClient for consumption by the TAF server
        qInfo() << "[main] connecting game status events to gpgnet";
        ForwardGameEventsToGpgNet gameEventsToGpgNet(gpgNetClient, [&parser](QString mapName) {
            return getMapDetails(parser.value("gamepath"), MAP_TOOL_EXE, mapName);
        });
        lobby.connectGameEvents(gameEventsToGpgNet);

        qInfo() << "[main] connecting game status events to HandleGameStatus";
        HandleGameStatus handleGameStatus(ircForward.get(), ircChannel, lobby);
        lobby.connectGameEvents(handleGameStatus);

        if (ircForward)
        {
            qInfo() << "[main] connecting IRC to lobby";
            QObject::connect(ircForward.get(), &IrcConnection::privateMessageReceived, [&lobby, &launcher](IrcPrivateMessage* msg)
            {
                try
                {
                    TADemo::Watchdog wd("main::privateMessageReceived", 100);
                    lobby.echoToGame(msg->isPrivate(), msg->nick(), msg->content());
                }
                catch (std::exception &e)
                {
                    qInfo() << "[main::privateMessageReceived] exception" << e.what();
                }
                catch (...)
                {
                    qInfo() << "[main::privateMessageReceived] unknown exception";
                }
            });

            QObject::connect(&launcher, &GpgNetGameLauncher::gameLaunched, ircForward.get(), [ircForward, ircChannel]()
            {
                ircForward->join(ircChannel);
                ircForward->open();
            });
        }

        ConsoleReader consoleReader(QHostAddress("127.0.0.1"), parser.value("consoleport").toInt());
        QObject::connect(&consoleReader, &ConsoleReader::textReceived, &launcher, &GpgNetGameLauncher::onExtendedMessage);
        app.exec();
    }
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
