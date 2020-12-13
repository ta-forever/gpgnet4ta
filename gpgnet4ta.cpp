#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qdatetime.h>
#include <QtCore/qsettings.h>
#include <QtCore/qfile.h>
#include <QtCore/qdir.h>
#include <QtCore/quuid.h>

#define MINIUPNP_STATICLIB
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

#include <fstream>
#include <sstream>

#include "gpgnet/GpgNetClient.h"
#include "jdplay/JDPlay.h"
#include "tademo/TADemoParser.h"
#include "tafnet/TafnetGameNode.h"

#include "GpgNetGameLauncher.h"
#include "IrcForward.h"
#include "TaLobby.h"

using namespace gpgnet;

bool CheckDplayLobbyableApplication(QString guid, QString path, QString file, QString commandLine, QString currentDirectory)
{
    QString registryPath = QString(R"(%1\%2)").arg(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay)", "Applications");
    QSettings registry(registryPath, QSettings::NativeFormat);
    QStringList applications = registry.childGroups();

    qInfo() << "\nCHECK:" << guid << path << file << commandLine << currentDirectory;
    Q_FOREACH(QString appName, applications)
    {
        QString nthGuid = registry.value(appName + "/Guid").toString();
        QString nthPath = registry.value(appName + "/Path").toString();
        QString nthFile = registry.value(appName + "/File").toString();
        QString nthCommandLine = registry.value(appName + "/CommandLine").toString();
        QString nthCurrentDirectory = registry.value(appName + "/CurrentDirectory").toString();
        if (QString::compare(guid, nthGuid, Qt::CaseInsensitive) == 0 &&
            QString::compare(path, nthPath, Qt::CaseInsensitive) == 0 &&
            QString::compare(file, nthFile, Qt::CaseInsensitive) == 0 &&
            QString::compare(commandLine, nthCommandLine) == 0 &&
            QString::compare(currentDirectory, nthCurrentDirectory, Qt::CaseInsensitive) ==0)
        {
            qInfo() << "MATCH:" << nthGuid << nthPath << nthFile << nthCommandLine << nthCurrentDirectory;
            return true;
        }
        qInfo() << "NO MATCH:" << nthGuid << nthPath << nthFile << nthCommandLine << nthCurrentDirectory;

    }
    return false;
}

void RegisterDplayLobbyableApplication(QString name, QString guid, QString path, QString file, QString commandLine, QString currentDirectory)
{
    QString registryPath = QString(R"(%1\%2)").arg(
        R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay\Applications)", name);

    QSettings registry(registryPath, QSettings::NativeFormat);
    registry.setValue("Guid", guid);
    registry.setValue("Path", path);
    registry.setValue("File", file);
    registry.setValue("CommandLine", commandLine);
    registry.setValue("CurrentDirectory", currentDirectory);
}

QString GetDplayLobbableAppPath(QString appGuid, QString defaultPath)
{
    QString registryPath = QString(R"(%1\%2)").arg(
        R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay)", "Applications");
    QSettings registry(registryPath, QSettings::NativeFormat);
    QStringList applications = registry.childGroups();
    Q_FOREACH(QString appName, applications)
    {
        QString nthGuid = registry.value(appName + "/Guid").toString();
        if (QString::compare(appGuid, nthGuid) == 0)
        {
            return registry.value(appName + "/Path").toString();
        }
    }
    return defaultPath;
}

class UPNPPortMapping
{
    WSADATA wsaData;
    char lan_address[64];
    struct UPNPUrls upnp_urls;
    struct IGDdatas upnp_data;
    char wan_address[64];

    std::string serviceDescription;
    std::string externalPort;
    std::string localPort;
    std::string tcpOrUdp;

public:
    UPNPPortMapping(const char* _serviceDescription, const char* _externalPort, const char* _localPort, const char* _tcpOrUdp):
        serviceDescription(_serviceDescription),
        externalPort(_externalPort),
        localPort(_localPort),
        tcpOrUdp(_tcpOrUdp)
    {
        int nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (nResult != NO_ERROR)
        {
            throw std::runtime_error("WSAStartup() failed");
        }

        int error = 0;
        struct UPNPDev* upnp_dev = upnpDiscover(
            2000, // time to wait (milliseconds)
            nullptr, // multicast interface (or null defaults to 239.255.255.250)
            nullptr, // path to minissdpd socket (or null defaults to /var/run/minissdpd.sock)
            0, // source port to use (or zero defaults to port 1900)
            0, // 0==IPv4, 1==IPv6
            2,
            &error); // error condition
        if (error != 0)
        {
            throw std::runtime_error("Unable to discover UPNP network devices");
        }

        int status = UPNP_GetValidIGD(upnp_dev, &upnp_urls, &upnp_data, lan_address, sizeof(lan_address));
        // look up possible "status" values, the number "1" indicates a valid IGD was found
        if (status != 1)
        {
            throw std::runtime_error("No Internet Gateway Device (IGD) found");
        }

        // get the external (WAN) IP address
        error = UPNP_GetExternalIPAddress(upnp_urls.controlURL, upnp_data.first.servicetype, wan_address);
        if (error != 0)
        {
            throw std::runtime_error("Unable to determine WAN IP address");
        }

        // add a new TCP port mapping from WAN port 12345 to local host port 24680
        error = UPNP_AddPortMapping(
            upnp_urls.controlURL,
            upnp_data.first.servicetype,
            externalPort.c_str(),
            localPort.c_str(),
            lan_address, // internal (LAN) address to which packets will be redirected
            serviceDescription.c_str(),
            tcpOrUdp.c_str(),
            nullptr, // remote (peer) host address or nullptr for no restriction
            "86400"); // port map lease duration (in seconds) or zero for "as long as possible"
    }

    ~UPNPPortMapping()
    {
        UPNP_DeletePortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, externalPort.c_str(), tcpOrUdp.c_str(), nullptr);
        WSACleanup();
    }
};


class ForwardGameEventsToGpgNet : public GameEventHandlerQt
{
    gpgnet::GpgNetClient &m_gpgNetClient;

public:
    ForwardGameEventsToGpgNet(gpgnet::GpgNetClient &gpgNetClient) :
        m_gpgNetClient(gpgNetClient)
    { }

    virtual void onGameSettings(QString mapName, quint16 maxUnits)
    {
        m_gpgNetClient.gameOption("MapName", mapName);
    }

    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 side, bool isDead, quint8 armyNumber, quint8 _teamNumber, QStringList mutualAllies)
    {
        QString gpgnetId = QString::number(m_gpgNetClient.lookupPlayerId(name));

        // Forged Alliance reserves Team=1 for the team-not-selected team
        int teamNumber = TADemo::Side(side) == TADemo::Side::WATCH ? -1 : int(_teamNumber);
        m_gpgNetClient.playerOption(gpgnetId, "Team", teamNumber);
        m_gpgNetClient.playerOption(gpgnetId, "StartSpot", armyNumber);
        m_gpgNetClient.playerOption(gpgnetId, "Army", armyNumber);
        m_gpgNetClient.playerOption(gpgnetId, "Faction", side);
    }

    virtual void onGameStarted(quint32 tick, bool teamsFrozen)
    {
        if (teamsFrozen)
        {
            m_gpgNetClient.gameState("Launching");
        }
    }

    virtual void onGameEnded(QMap<qint32, qint32> resultByArmy, QMap<QString, qint32> armyNumbersByPlayerName)
    {
        for (auto it = resultByArmy.begin(); it != resultByArmy.end(); ++it)
        {
            m_gpgNetClient.gameResult(it.key(), it.value());
        }
        m_gpgNetClient.gameState("Ended");
    }

    virtual void onChat(QString msg, bool isLocalPlayerSource)
    { }
};


class HandleGameStatus : public GameEventHandlerQt
{
    IrcForward& m_irc;
    QString m_channel;
    TaLobby& m_taLobby;

public:
    HandleGameStatus(IrcForward& irc, QString channel, TaLobby& taLobby) :
        m_irc(irc),
        m_channel(channel),
        m_taLobby(taLobby)
    { }

    virtual void onGameSettings(QString mapName, quint16 maxUnits)
    {
        doSend("Map changed to: " + mapName.toStdString(), true, false);
    }

    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 side, bool isDead, quint8 armyNumber, quint8 _teamNumber, QStringList mutualAllies)
    { }

    virtual void onGameStarted(quint32 tick, bool teamsFrozen)
    {
        std::ostringstream ss;
        if (!teamsFrozen)
        {
            ss << "GAME STARTED WOOOO! Still time to set your teams";
        }
        else
        {
            ss << "TEAMS FROZEN, GAME ON! Good luck commanders!";
        }
        doSend(ss.str(), true, true);
    }

    virtual void onGameEnded(QMap<qint32, qint32> resultByArmy, QMap<QString, qint32> armyNumbersByPlayerName)
    {
        doSend("Game over man, game over!", true, true);
        for (auto it = armyNumbersByPlayerName.begin(); it != armyNumbersByPlayerName.end(); ++it)
        {
            std::ostringstream ss;
            qint32 resultInt = resultByArmy[it.value()];
            std::string resultString;
            if (resultInt > 0)
                resultString = "VICTORY";
            else if (resultInt == 0)
                resultString = "DRAW";
            else
                resultString = "DEFEAT";

            ss << it.key().toStdString() << ": " << resultString;;
            doSend(ss.str(), true, true);
        }
    }

    virtual void onChat(QString msg, bool isLocalPlayerSource)
    {
        if (isLocalPlayerSource)
        {
            int n = msg.lastIndexOf("> ");
            if (msg[0] == '<' && n>=0 && n+2 < msg.size())
            {
                // remove the <name> tag so player doesn't get pinged in IRC
                msg = msg.mid(n + 2);
            }
            doSend(msg.toStdString(), true, false);
        }
    }

private:
    void doSend(const std::string & chat, bool toIrc, bool toGame)
    {
        if (toIrc)
        {
            m_irc.sendCommand(IrcCommand::createMessage(m_channel, chat.c_str()));
        }
        if (toGame)
        {
            m_taLobby.echoToGame("", QString::fromStdString(chat));
        }
    }
};


class Logger
{
public:
    enum class Verbosity { SILENT = 0, FATAL = 1, WARNING = 2, CRITICAL = 3, INFO = 4, DEBUG = 5 };

private:
    std::ofstream m_logfile;
    Verbosity m_verbosity;
    static std::shared_ptr<Logger> m_instance;

public:
    Logger(const std::string &filename, Verbosity verbosity) :
        m_logfile(filename, std::iostream::out),
        m_verbosity(verbosity)
    { }

    static void Initialise(const std::string &filename, Verbosity level)
    {
        m_instance.reset(new Logger(filename, level));
    }

    static Logger * Get()
    {
        return m_instance.get();
    }

    static void Log(QtMsgType type, const QMessageLogContext &context, const QString &msg)
    {
        Logger::Get()->LogToFile(type, context, msg);
    }

    void LogToFile(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
        switch (type) {
        case QtDebugMsg:
            if (m_verbosity >= Verbosity::DEBUG)
            {
                QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
                m_logfile << datetime.toStdString() << " [Debug] " << msg.toStdString() << std::endl;
            }
            break;
        case QtInfoMsg:
            if (m_verbosity >= Verbosity::INFO)
            {
                QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
                m_logfile << datetime.toStdString() << " [Info] " << msg.toStdString() << std::endl;
            }
            break;
        case QtCriticalMsg:
            if (m_verbosity >= Verbosity::CRITICAL)
            {
                QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
                m_logfile << datetime.toStdString() << " [Critical] " << msg.toStdString() << std::endl;
            }
            break;
        case QtWarningMsg:
            if (m_verbosity >= Verbosity::WARNING)
            {
                QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
                m_logfile << datetime.toStdString() << " [Warning] " << msg.toStdString() << std::endl;
            }
            break;
        case QtFatalMsg:
            if (m_verbosity >= Verbosity::FATAL)
            {
                QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
                m_logfile << datetime.toStdString() << " [Fatal] " << msg.toStdString() << std::endl;
            }
            abort();
        }
    }
};

std::shared_ptr<Logger> Logger::m_instance;


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


void RunAs(QString cmd, QStringList args)
{
    cmd = '"' + cmd + '"';
    std::transform(args.begin(), args.end(), args.begin(), [](QString arg) -> QString { return '"' + arg + '"'; });
    QString joinedArgs = args.join(' ');

    std::string _cmd = cmd.toStdString();
    std::string _args = joinedArgs.toStdString();

    SHELLEXECUTEINFO ShExecInfo = { 0 };
    ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    ShExecInfo.hwnd = NULL;
    ShExecInfo.lpVerb = "runas";
    ShExecInfo.lpFile = _cmd.c_str();
    ShExecInfo.lpParameters = _args.c_str();
    ShExecInfo.lpDirectory = NULL;
    ShExecInfo.nShow = SW_HIDE;
    ShExecInfo.hInstApp = NULL;
    ShellExecuteEx(&ShExecInfo);
    WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
    CloseHandle(ShExecInfo.hProcess);
}

int main(int argc, char* argv[])
{
    const char *DEFAULT_GAME_INI_TEMPLATE = "TAForever.ini.template";
    const char* DEFAULT_GAME_INI = "TAForever.ini";
    const char *ADDITIONAL_GAME_ARGS = "-c TAForever.ini";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_GUID = "{1336f32e-d116-4633-b853-4fee1ec91ea5}";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_EXE = "totala.exe";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_PATH = "c:\\cavedog\\totala";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_ARGS = "";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_MOD = "TACC";

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("GpgPlay");
    QCoreApplication::setApplicationVersion("0.7");

    QCommandLineParser parser;
    parser.setApplicationDescription("GPGNet facade for Direct Play games");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("gpgnet", "Uri to GPGNet.", "host:port"));
    parser.addOption(QCommandLineOption("mean", "Player rating mean.", "mean"));
    parser.addOption(QCommandLineOption("deviation", "Player rating deviation.", "deviation"));
    parser.addOption(QCommandLineOption("country", "Player country code.", "code"));
    parser.addOption(QCommandLineOption("numgames", "Player game count.", "count"));
    parser.addOption(QCommandLineOption("players", "Max number of players 2 to 10", "players", "10"));
    parser.addOption(QCommandLineOption("lockoptions", "Lock (some of) the lobby options"));
    parser.addOption(QCommandLineOption("registerdplay", "Register the dplay lobbyable app with --gamepath, --gameexe, --gameargs. (requires run as admin)."));
    parser.addOption(QCommandLineOption("upnp", "Attempt to set up a port forward using UPNP"));
    parser.addOption(QCommandLineOption("gamepath", "Path from which to launch game. (required for --registerdplay).", "path", DEFAULT_DPLAY_REGISTERED_GAME_PATH));
    parser.addOption(QCommandLineOption("gameexe", "Game executable. (required for --registerdplay).", "exe", DEFAULT_DPLAY_REGISTERED_GAME_EXE));
    parser.addOption(QCommandLineOption("gameargs", "Command line arguments for game executable. (required for --registerdplay).", "args", DEFAULT_DPLAY_REGISTERED_GAME_ARGS));
    parser.addOption(QCommandLineOption("gamemod", "Name of the game variant (used to generate a DirectPlay registration that doesn't conflict with another variant", "gamemod", DEFAULT_DPLAY_REGISTERED_GAME_MOD));
    parser.addOption(QCommandLineOption("irc", "user@host:port/channel for the ingame irc channel to join", "irc"));
    parser.addOption(QCommandLineOption("lobbybindaddress", "Interface on which to bind the lobby interface", "lobbybindaddress", "127.0.0.1"));
    parser.addOption(QCommandLineOption("createlobby", "Test launch a game.  if no 'joingame' option given, test launch as host"));
    parser.addOption(QCommandLineOption("joingame", "When test launching, join game hosted at specified ip", "joingame", "0.0.0.0"));
    parser.addOption(QCommandLineOption("connecttopeer", "When test launching, list of peers (excluding the host) to connect to", "connecttopeer"));
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs", "logfile", "c:\\temp\\gpgnet4ta.log"));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug)", "logfile", "4"));
    parser.addOption(QCommandLineOption("uac", "run as admin"));
    parser.process(app);

    if (parser.isSet("uac"))
    {
        QStringList args;
        for (const char *arg : {
            "gpgnet", "mean", "deviation", "country", "numgames", "players",
            "gamepath", "gameexe", "gameargs", "gamemod", "lobbybindaddress", "joingame", "connecttopeer",
            "logfile", "loglevel", "irc" })
        {
            if (parser.isSet(arg))
            {
                args << (QString("--") + arg) << parser.value(arg);
            }
        }
        for (const char *arg : {"lockoptions", "upnp", "createlobby"})
        {
            if (parser.isSet(arg))
            {
                args << QString("--") + arg;
            }
        }
        args << "--registerdplay";  // once we have UAC, just register regardless of current registry setting - avoid re-asking for UAC

        RunAs(app.applicationFilePath(), args);
        return 0;
    }

    Logger::Initialise(parser.value("logfile").toStdString(), Logger::Verbosity(parser.value("loglevel").toInt()));
    qInstallMessageHandler(Logger::Log);

    QString dplayGuid = QUuid::createUuidV5(QUuid(DEFAULT_DPLAY_REGISTERED_GAME_GUID), parser.value("gamemod").toUpper()).toString();
    QString dplayAppName = "Total Annihilation Forever (" + parser.value("gamemod").toUpper() + ")";
    QString dplayGameArgs;
    if (parser.value("gameargs").isEmpty() || parser.value("gameargs").contains("-c", Qt::CaseInsensitive))
    {
        // don't allow user to specify their own config file.  We want control of that ...
        dplayGameArgs = ADDITIONAL_GAME_ARGS;
    }
    else
    {
        dplayGameArgs = parser.value("gameargs") + ' ' + ADDITIONAL_GAME_ARGS;
    }

    if (!QFileInfo(QDir(parser.value("gamepath")), parser.value("gameexe")).isExecutable())
    {
        QString err = QString("Unable to find ") + parser.value("gamemod").toUpper() + " at path \"" + parser.value("gamepath") + "\\" + parser.value("gameexe") + '"';
        err += "\nPlease check your game settings";
        MessageBox(
            NULL,
            err.toStdString().c_str(),
            "Unable to launch game",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    if (parser.isSet("registerdplay"))
    {
        RegisterDplayLobbyableApplication(
            dplayAppName, dplayGuid, parser.value("gamepath"), parser.value("gameexe"), dplayGameArgs, parser.value("gamepath"));
    }

    else if (!CheckDplayLobbyableApplication(
        dplayGuid, parser.value("gamepath"), parser.value("gameexe"), dplayGameArgs, parser.value("gamepath")))
    {
        QStringList args;
        args << "--registerdplay";
        args << "--gamemod" << parser.value("gamemod");
        args << "--gamepath" << parser.value("gamepath");
        args << "--gameexe" << parser.value("gameexe");
        args << "--gameargs" << parser.value("gameargs");

        RunAs(app.applicationFilePath(), args);
        while (!CheckDplayLobbyableApplication(dplayGuid, parser.value("gamepath"), parser.value("gameexe"), dplayGameArgs, parser.value("gamepath")))
        {
            QString err = QString("Unable to update DirectPlay registration for ") + parser.value("gamemod").toUpper() + " at path \"" + parser.value("gamepath") + "\\" + parser.value("gameexe") + "\". ";
            err += "Probably gpgnet4ta was unable to gain admin privileges. Attempt to launch anyway?";
            int result = MessageBox(
                NULL,
                err.toStdString().c_str(),
                "DirectPlay Registration failed",
                MB_ABORTRETRYIGNORE | MB_ICONERROR
            );

            if (result == IDABORT)
                return 1;
            else if (result == IDIGNORE)
                break;

            RunAs(app.applicationFilePath(), args);
        } 
    }

    QString serviceName = "GPGNet4TA / Total Annihilation";
    QSharedPointer<UPNPPortMapping> port47624;
    while (parser.isSet("upnp") && port47624.isNull())
    {
        try
        {
            port47624.reset(new UPNPPortMapping(serviceName.toStdString().c_str(), "47624", "47624", "TCP"));
        }
        catch (std::runtime_error & e)
        {
            QString err = QString("Unable to activate UPNP port forwarding: ") + e.what() + ".\n\n";
            err += "Disable UPNP in Settings menu if you don't want to use UPNP to forward your ports in future.\n\n";
            err += "If you need to set up port forwarding manually to play Total Annihilation online, see https://portforward.com/total-annihilation";
            int result = MessageBox(
                NULL,
                err.toStdString().c_str(),
                "UPNP Port Forward failed",
                MB_ABORTRETRYIGNORE | MB_ICONERROR
            );

            if (result == IDABORT)
                return 1;
            else if (result == IDIGNORE)
                break;
        }
    }

    if (parser.isSet("createlobby"))
    {
        TaLobby lobby(QUuid(dplayGuid), parser.value("lobbybindaddress"), "127.0.0.1", "127.0.0.1");
        QString playerName = parser.value("lobbybindaddress").split(':')[0];
        std::uint32_t playerId = QHostAddress(playerName).toIPv4Address() & 0xff;
        lobby.onCreateLobby(0, 6112, parser.value("lobbybindaddress"), playerId, 0);

        if (parser.isSet("joingame"))
        {
            QString hostaddr = parser.value("joingame");
            QString playerName = hostaddr.split(':')[0];
            std::uint32_t playerId = QHostAddress(playerName).toIPv4Address() & 0xff;
            lobby.onJoinGame(hostaddr, playerName, playerId);
        }
        for (QString peer : parser.values("connecttopeer"))
        {
            QString playerName = peer.split(':')[0];
            std::uint32_t playerId = QHostAddress(playerName).toIPv4Address() & 0xff;
            lobby.onConnectToPeer(peer, playerName, playerId);
        }

        const bool createAsHost = !parser.isSet("joingame");
        JDPlay jdplay(playerName.toStdString().c_str(), 3, false);
        bool ret = jdplay.initialize(dplayGuid.toStdString().c_str(), createAsHost ? "0.0.0.0" : "127.0.0.1", createAsHost, 10);

        if (!ret)
        {
            qInfo() << "unable to initialise jdplay";
            return 1;
        }
        ret = jdplay.launch(true);
        if (!ret)
        {
            qInfo() << "unable to launch jdplay";
            return 1;
        }

        QTimer stopTimer;
        QObject::connect(&stopTimer, &QTimer::timeout, &app, [&jdplay, &app]() {
            if (!jdplay.pollStillActive())
            {
                app.quit();
            }
        });
        stopTimer.start(1000);

        app.exec();
    }

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
        ircForward->join(ircChannel);
        ircForward->open();
    }

    if (parser.isSet("gpgnet"))
    {
        // not from the command line, from the dplay registry since thats what we're actually going to launch
        QString gamePath = GetDplayLobbableAppPath(dplayGuid, parser.value("gamepath"));

        // GpgNetClient receives from gpgnet instructions to host/join/connect to peers; GpgNetClient sends back to gpgnet game and player status
        gpgnet::GpgNetClient gpgNetClient(parser.value("gpgnet"));

        // JDPlay is a wrapper around directplay game launch and enumeration.
        // We don't use enumeration because it causes our ice adapter proxy stuff to go haywire.
        // Instead we have blind faith that gpgnet/ice adapter know what they're doing
        // And we snoop network traffic to work out everything else (see TaLobby).
        JDPlay jdplay("BILLYIDOL", 3, false);

        // GpgNetGameLauncher interprets instructions from GpgNetClient to launch TA as a host or as a joiner
        GpgNetGameLauncher launcher(
            DEFAULT_GAME_INI_TEMPLATE,
            gamePath + "\\" + DEFAULT_GAME_INI,
            dplayGuid,
            parser.value("players").toInt(),
            parser.isSet("lockoptions"),
            jdplay,
            gpgNetClient);

        // TaLobby is a conglomerate of objects that handles a man-in-the-middle relay of TA network traffic
        // Together they work to tunnel everything through a single UDP port
        // That UDP port is expected to be one brokered by the FAF ICE adapter independently of gpgnet4ta
        // TaLobby needs to be told explicetly to whom connections are to be made and on which UDP ports peers can be found
        // (viz all the Qt signal connections from GpgNetClient to TaLobby)
        TaLobby lobby(QUuid(dplayGuid), "127.0.0.1", "127.0.0.1", "127.0.0.1");
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::createLobby, &lobby, &TaLobby::onCreateLobby);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::joinGame, &lobby, &TaLobby::onJoinGame);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::connectToPeer, &lobby, &TaLobby::onConnectToPeer);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::disconnectFromPeer, &lobby, &TaLobby::onDisconnectFromPeer);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::createLobby, &launcher, &GpgNetGameLauncher::onCreateLobby);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::hostGame, &launcher, &GpgNetGameLauncher::onHostGame);
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::joinGame, &launcher, &GpgNetGameLauncher::onJoinGame);
        QObject::connect(&launcher, &GpgNetGameLauncher::gameTerminated, &app, &QCoreApplication::quit);
        QObject::connect(&launcher, &GpgNetGameLauncher::gameFailedToLaunch, &app, [&app, &parser]() {
            QString err = QString("Unable to launch ") + parser.value("gamemod").toUpper() + " at path \"" + parser.value("gamepath") + "\\" + parser.value("gameexe") + "\"\n";
            err +=
                "- Please check path is correct\n"
                "  Correct the path in TAF Settings menu if not\n"
                "- Please check you can launch game outside of TAF\n"
                "- Try enable 'Run TA as Admin' in TAF Settings menu";
            MessageBox(
                NULL,
                err.toStdString().c_str(),
                "Unable to launch game",
                MB_OK | MB_ICONERROR
            );
            app.quit();
        });

        // The TaLobby also takes the opporunity to snoop the network traffic that it handles in order to work out whats happening in the game
        // (eg game started/ended state; selected map, players and teams at start of game; and winners/losers/draws at end of game)
        // This information is passed on to the GpgNetClient for consumption by the TAF server
        ForwardGameEventsToGpgNet gameEventsToGpgNet(gpgNetClient);
        lobby.connectGameEvents(gameEventsToGpgNet);

        std::shared_ptr<HandleGameStatus> handleGameStatus;
        if (ircForward)
        {
            qInfo() << "[main] connecting lobby to IRC";
            handleGameStatus.reset(new HandleGameStatus(*ircForward, ircChannel, lobby));
            lobby.connectGameEvents(*handleGameStatus);

            qInfo() << "[main] connecting IRC to lobby";
            QObject::connect(ircForward.get(), &IrcConnection::privateMessageReceived, [&lobby](IrcPrivateMessage* msg)
            {
                lobby.echoToGame(msg->nick(), msg->content());
            });
        }
        app.exec();
    }
    return 0;
}
