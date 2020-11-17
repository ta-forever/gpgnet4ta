#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qdatastream.h>
#include <QtCore/qsettings.h>
#include <QtCore/qfile.h>
#include <QtCore/qdir.h>
#include <QtCore/qthread.h>
#include <QtCore/quuid.h>
#include <QtNetwork/qhostinfo.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qnetworkinterface.h>

#define MINIUPNP_STATICLIB
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

#include <algorithm>
#include <fstream>

#include "jdplay/JDPlay.h"
#include "gpgnet/GpgNetReceive.h"
#include "gpgnet/GpgNetSend.h"
#include "tademo/TADemoParser.h"
#include "tademo/GameMonitor.h"
#include "tafnet/TafnetGameNode.h"

#include "GpgNetRunner.h"
#include "TaLobby.h"

using namespace gpgnet;


bool CheckDplayLobbyableApplication(QString guid, QString path, QString file, QString commandLine, QString currentDirectory)
{
    QString registryPath = QString(R"(%1\%2)").arg(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay)", "Applications");
    QSettings registry(registryPath, QSettings::NativeFormat);
    QStringList applications = registry.childGroups();
    //qDebug() << "\nCHECK:" << guid << path << file << commandLine << currentDirectory;
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
            //qDebug() << "MATCH:" << nthGuid << nthPath << nthFile << nthCommandLine << nthCurrentDirectory;
            return true;
        }
        //qDebug() << "NO MATCH:" << nthGuid << nthPath << nthFile << nthCommandLine << nthCurrentDirectory;

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
    const char* DEFAULT_DPLAY_PROTOCOL = "TCP";

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("GpgPlay");
    QCoreApplication::setApplicationVersion("1.0");

    // GpgPlay.exe /gamepath d:\games\ta /gameexe totala.exe /gameargs "-d -c d:\games\taforever.ini" /gpgnet 127.0.0.1:37135 /mean 1500 /deviation 75 /savereplay gpgnet://127.0.0.1:50703/12797031/Axle.SCFAreplay /country AU /numgames 878
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
    parser.addOption(QCommandLineOption("testlaunch", "Launch TA straight away."));
    parser.process(app);

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
        QString cmd = "\"" + app.applicationFilePath() + "\"";
        QStringList args;
        args << "--registerdplay";
        args << "--gamemod" << parser.value("gamemod");
        args << "--gamepath" << parser.value("gamepath");
        args << "--gameexe" << parser.value("gameexe");
        args << "--gameargs" << parser.value("gameargs");
        std::transform(args.begin(), args.end(), args.begin(), [](QString arg) -> QString { return "\"" + arg + "\""; });
        QString joinedArgs = args.join(' ');

        ShellExecute(GetConsoleWindow(), "runas", cmd.toStdString().c_str(), joinedArgs.toStdString().c_str(), 0, SW_HIDE);
        QThread::msleep(300);
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

             ShellExecute(GetConsoleWindow(), "runas", cmd.toStdString().c_str(), joinedArgs.toStdString().c_str(), 0, SW_HIDE);
             QThread::msleep(300);
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

    if (parser.isSet("testlaunch"))
    {
        CreateLobbyCommand createLobbyCommand;
        JDPlay jdplay(createLobbyCommand.playerName.toStdString().c_str(), 3, false);
        bool ret = jdplay.initialize(dplayGuid.toStdString().c_str(), "0.0.0.0", true, 10);
        if (!ret)
        {
            qDebug() << "unable to initialise jdplay";
            return 1;
        }
        ret = jdplay.launch(true);
        if (!ret)
        {
            qDebug() << "unable to launch jdplay";
            return 1;
        }
        while (jdplay.pollStillActive())
        {
            QThread::msleep(1000);
        };
    }

    if (parser.isSet("gpgnet"))
    {
        // not from the command line, from the dplay registry since thats what we're actually going to launch
        QString gamePath = GetDplayLobbableAppPath(dplayGuid, parser.value("gamepath"));

        GpgNetRunner gpgnet(
            DEFAULT_GAME_INI_TEMPLATE,
            gamePath + "\\" + DEFAULT_GAME_INI,
            parser.value("gpgnet"),
            parser.value("mean").toDouble(), parser.value("deviation").toDouble(),
            parser.value("country"), parser.value("numgames").toInt(),
            dplayGuid,
            parser.value("players").toInt(),
            parser.isSet("lockoptions"));

        TaLobby lobby("127.0.0.1", 0, "127.0.0.1", "127.0.0.1");
        QObject::connect(&gpgnet, &GpgNetRunner::createLobby, &lobby, &TaLobby::onCreateLobby);
        QObject::connect(&gpgnet, &GpgNetRunner::joinGame, &lobby, &TaLobby::onJoinGame);
        QObject::connect(&gpgnet, &GpgNetRunner::connectToPeer, &lobby, &TaLobby::onConnectToPeer);
        QObject::connect(&gpgnet, &GpgNetRunner::finished, &app, &QCoreApplication::quit);

        gpgnet.start();
        app.exec();
    }
    return 0;
}
