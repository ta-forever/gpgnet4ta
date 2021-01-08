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
#include <iostream>
#include <sstream>

#include "gpgnet/GpgNetClient.h"
#include "jdplay/JDPlay.h"
#include "tademo/TADemoParser.h"
#include "tafnet/TafnetGameNode.h"

#include "ConsoleReader.h"
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
    bool m_isHost;

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
    ForwardGameEventsToGpgNet(gpgnet::GpgNetClient &gpgNetClient) :
        m_gpgNetClient(gpgNetClient)
    { }

    virtual void onGameSettings(QString mapName, quint16 maxUnits, QString hostName, QString localName)
    {
        try
        {
            qInfo() << "[ForwardGameEventsToGpgNet::onPlayerStatus] GameOption" << mapName << "host" << hostName << "local" << localName;
            m_isHost = hostName == localName;
            if (m_isHost)
            {
                m_gpgNetClient.gameOption("MapName", mapName);
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

    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 slot, quint8 side, bool isAI, bool isDead, quint8 armyNumber, quint8 _teamNumber, QStringList mutualAllies)
    {
        try
        {
            QString gpgnetId = QString::number(m_gpgNetClient.lookupPlayerId(name));

            // Forged Alliance reserves Team=1 for the team-not-selected team
            if (isAI || gpgnetId == 0)
            {
                int teamNumber = int(_teamNumber);
                QString aiName = makeAiName(name, slot);
                qInfo() << "[ForwardGameEventsToGpgNet::onPlayerStatus] AiOption" << aiName << "slot" << slot << "army" << armyNumber << "team" << teamNumber << "side" << side << "isDead" << isDead;
                if (m_isHost)
                {
                    m_gpgNetClient.aiOption(aiName, "Team", teamNumber);
                    m_gpgNetClient.aiOption(aiName, "StartSpot", slot);
                    m_gpgNetClient.aiOption(aiName, "Army", armyNumber);
                    m_gpgNetClient.aiOption(aiName, "Faction", side);
                }
            }
            else
            {
                int teamNumber = TADemo::Side(side) == TADemo::Side::WATCH ? -1 : int(_teamNumber);
                qInfo() << "[ForwardGameEventsToGpgNet::onPlayerStatus] PlayerOption" << name << "id" << gpgnetId << "slot" << slot << "army" << armyNumber << "team" << teamNumber << "side" << side << "isDead" << isDead;
                if (m_isHost)
                {
                    m_gpgNetClient.playerOption(gpgnetId, "Team", teamNumber);
                    m_gpgNetClient.playerOption(gpgnetId, "StartSpot", slot);
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

public:
    HandleGameStatus(IrcForward* irc, QString channel, TaLobby& taLobby) :
        m_irc(irc),
        m_channel(channel),
        m_taLobby(taLobby),
        m_quitRequested(0),
        m_playerNames(10u),
        m_isAI(10u, false)
    { }

    virtual void onGameSettings(QString mapName, quint16 maxUnits, QString hostName, QString localName)
    {
        try
        {
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

    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 slot, quint8 side, bool isAI, bool isDead, quint8 armyNumber, quint8 _teamNumber, QStringList mutualAllies)
    {
        try
        {
            if (slot < m_playerNames.size())
            {
                m_playerNames[slot] = name;
                m_isAI[slot] = isAI;
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
            if (slot < m_playerNames.size())
            {
                m_playerNames[slot].clear();
                m_isAI[slot] = false;
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
            const int occupancyCount = getOccupancyCount();
            const int aiCount = getAiCount();
            const int humanCount = occupancyCount - aiCount;

            if (!teamsFrozen && aiCount == 0)
            {
                if (humanCount >= 3)
                {
                    doSend("Game becomes rated after 1:00. Finalise teams by then", false, true);
                    doSend("Self-d/alt-f4 before to rescind, or ally afterwards", false, true);
                    doSend("/quit to disconnect so team can .take (unless UR host!)", false, true);
                }
                else if (humanCount == 2)
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
            doSend("Game over man, game over!", true, true);
            for (const QVariantMap &result : results)
            {
                std::ostringstream ss;
                std::string name = result.value("name").toString().toStdString();
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

                ss << name << ": " << outcome;
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
            if (isLocalPlayerSource && msg.size() > 0)
            {
                int n = msg.lastIndexOf("> ");
                if (msg[0] == '<' && n >= 0 && n + 2 < msg.size())
                {
                    // remove the <name> tag so player doesn't get pinged in IRC
                    msg = msg.mid(n + 2);
                }

                doSend(msg.toStdString(), true, false);

                m_quitRequested = msg == "/quit" ? ++m_quitRequested : 0;
                if (m_quitRequested == 1)
                {
                    doSend("Type /quit again to disconnect your game", false, true);
                }
                else if (m_quitRequested == 2)
                {
                    doSend("ok your game is disconnected you can alt-f4", false, true);
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
            m_taLobby.echoToGame("", false, QString::fromStdString(chat));
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
    QCoreApplication::setApplicationVersion("0.10.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("GPGNet facade for Direct Play games");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("autolaunch", "Normally gpgnet4ta sets up the connections then waits for a /launch command before it launches TA. This option causes TA to launch straight away."));
    parser.addOption(QCommandLineOption("cmdfile", "gpgnet4ta will follow this file to find /quit and /launch commands.", "stdin"));
    parser.addOption(QCommandLineOption("country", "Player country code.", "code"));
    parser.addOption(QCommandLineOption("connecttopeer", "When test launching, list of peers (excluding the host) to connect to.", "connecttopeer"));
    parser.addOption(QCommandLineOption("createlobby", "Test launch a game.  if no 'joingame' option given, test launch as host"));
    parser.addOption(QCommandLineOption("deviation", "Player rating deviation.", "deviation"));
    parser.addOption(QCommandLineOption("gameargs", "Command line arguments for game executable. (required for --registerdplay).", "args", DEFAULT_DPLAY_REGISTERED_GAME_ARGS));
    parser.addOption(QCommandLineOption("gameexe", "Game executable. (required for --registerdplay).", "exe", DEFAULT_DPLAY_REGISTERED_GAME_EXE));
    parser.addOption(QCommandLineOption("gamemod", "Name of the game variant (used to generate a DirectPlay registration that doesn't conflict with another variant.", "gamemod", DEFAULT_DPLAY_REGISTERED_GAME_MOD));
    parser.addOption(QCommandLineOption("gamepath", "Path from which to launch game. (required for --registerdplay).", "path", DEFAULT_DPLAY_REGISTERED_GAME_PATH));
    parser.addOption(QCommandLineOption("gpgnet", "Uri to GPGNet.", "host:port"));
    parser.addOption(QCommandLineOption("irc", "user@host:port/channel for the ingame irc channel to join.", "irc"));
    parser.addOption(QCommandLineOption("joingame", "When test launching, join game hosted at specified ip.", "joingame", "0.0.0.0"));
    parser.addOption(QCommandLineOption("lobbybindaddress", "Interface on which to bind the lobby interface.", "lobbybindaddress", "127.0.0.1"));
    parser.addOption(QCommandLineOption("lockoptions", "Lock (some of) the lobby options."));
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs.", "logfile", "c:\\temp\\gpgnet4ta.log"));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug).", "logfile", "5"));
    parser.addOption(QCommandLineOption("mean", "Player rating mean.", "mean"));
    parser.addOption(QCommandLineOption("numgames", "Player game count.", "count"));
    parser.addOption(QCommandLineOption("players", "Max number of players 2 to 10.", "players", "10"));
    parser.addOption(QCommandLineOption("proactiveresend", "Measure packet-loss during game setup and thereafter send multiple copies of packets accordingly."));
    parser.addOption(QCommandLineOption("registerdplay", "Register the dplay lobbyable app with --gamepath, --gameexe, --gameargs. (requires run as admin)."));
    parser.addOption(QCommandLineOption("uac", "run as admin."));
    parser.addOption(QCommandLineOption("upnp", "Attempt to set up a port forward using UPNP."));
    parser.process(app);

    if (parser.isSet("uac"))
    {
        QStringList args;
        for (const char *arg : {
            "autolaunch", "gpgnet", "mean", "deviation", "country", "numgames", "players",
            "gamepath", "gameexe", "gameargs", "gamemod", "lobbybindaddress", "joingame", "connecttopeer",
            "logfile", "loglevel", "irc", "proactiveresend", "cmdfile" })
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
        TaLobby lobby(QUuid(dplayGuid), parser.value("lobbybindaddress"), "127.0.0.1", "127.0.0.1", parser.isSet("proactiveresend"));
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
            parser.value("gamemod").toUpper()=="TAESC" ? 1000 : 1500,
            jdplay,
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
            launcher.onHostGame(mapName);
            if (parser.isSet("autolaunch")) launcher.onLaunchGame();
        });
        QObject::connect(&gpgNetClient, &gpgnet::GpgNetClient::joinGame, [&launcher, &parser](QString host, QString playerName, int playerId) {
            launcher.onJoinGame(host, playerName, playerId);
            if (parser.isSet("autolaunch")) launcher.onLaunchGame();
        });
        QObject::connect(&launcher, &GpgNetGameLauncher::gameTerminated, &app, &QCoreApplication::quit);
        QObject::connect(&launcher, &GpgNetGameLauncher::gameFailedToLaunch, [&app, &parser]() {
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
        qInfo() << "[main] connecting game status events to gpgnet";
        ForwardGameEventsToGpgNet gameEventsToGpgNet(gpgNetClient);
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
        }

        std::shared_ptr<std::istream> ifstream;
        std::istream *istream = &std::cin; // NB stdin doesn't work properly with ConsoleReader :(
        if (parser.isSet("cmdfile"))
        {
            ifstream.reset(new std::ifstream(parser.value("cmdfile").toStdString()));
            istream = ifstream.get();
        }
        ConsoleReader consoleReader(*istream);
        QObject::connect(&consoleReader, &ConsoleReader::textReceived, &launcher, &GpgNetGameLauncher::onExtendedMessage);
        consoleReader.start();
        app.exec();
    }
    return 0;
}
