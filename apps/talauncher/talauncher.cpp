#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qsettings.h>
#include <QtCore/qdir.h>
#include <QtCore/quuid.h>

#include "taflib/Logger.h"
#include "taflib/MessageBoxThread.h"
#include "talaunch/LaunchServer.h"

#include <iostream>

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


QMap<QString, QString> GetDplayLobbableApp(QString appGuid)
{
    QMap<QString, QString> result;

    QString registryPath = QString(R"(%1\%2)").arg(
        R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay)", "Applications");
    QSettings registry(registryPath, QSettings::NativeFormat);
    QStringList applications = registry.childGroups();
    Q_FOREACH(QString appName, applications)
    {
        QString nthGuid = registry.value(appName + "/Guid").toString();
        if (QString::compare(appGuid, nthGuid) == 0)
        {
            result["Guid"] = nthGuid;
            result["Path"] = registry.value(appName + "/Path", QVariant("<no Path>")).toString();
            result["File"] = registry.value(appName + "/File", QVariant("<no File>")).toString();
            result["CommandLine"] = registry.value(appName + "/CommandLine", QVariant("<no CommandLine>")).toString();
            result["CurrentDirectory"] = registry.value(appName + "/CurrentDirectory", QVariant("<no CurrentDirectory>")).toString();
            return result;
        }
    }
    return result;
}


void RunAs(QString cmd, QStringList args, QString verb = "runas")
{
    cmd = '"' + cmd + '"';
    std::transform(args.begin(), args.end(), args.begin(), [](QString arg) -> QString { return '"' + arg + '"'; });
    QString joinedArgs = args.join(' ');

    std::string _cmd = cmd.toStdString();
    std::string _args = joinedArgs.toStdString();
    std::string _verb = verb.toStdString();

    SHELLEXECUTEINFO ShExecInfo = { 0 };
    ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    ShExecInfo.hwnd = NULL;
    ShExecInfo.lpVerb = _verb.c_str();
    ShExecInfo.lpFile = _cmd.c_str();
    ShExecInfo.lpParameters = _args.c_str();
    ShExecInfo.lpDirectory = NULL;
    ShExecInfo.nShow = SW_HIDE;
    ShExecInfo.hInstApp = NULL;
    ShellExecuteEx(&ShExecInfo);
    WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
    CloseHandle(ShExecInfo.hProcess);
}

void UnableToLaunchMsgBox(taflib::MessageBoxThread& msgbox, QString guid)
{
    QMap<QString, QString> appSettings = GetDplayLobbableApp(guid);
    QString err;
    if (appSettings.contains("Path") && appSettings.contains("File"))
    {
        err = "Unable to launch game at path \"" + appSettings["Path"] + "\\" + appSettings["File"] + "\"\n";
        err +=
            "- Please check path is correct\n"
            "  Correct the path in TAF Settings menu if not\n"
            "- Please check you can launch game outside of TAF\n"
            "- Try enable 'Run TA as Admin' in TAF Settings menu\n"
            "  (and restart TAF)";
    }
    else
    {
        err = "Unable to launch game.  There is no DirectPlay registry entry for game with guid=\"" + guid + "\"\n";
    }
    QMetaObject::invokeMethod(&msgbox, "onMessage", Qt::QueuedConnection, Q_ARG(QString, "TAForever"), Q_ARG(QString, err), Q_ARG(unsigned int, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL));
}

void GameExitedWithErrorMsgBox(taflib::MessageBoxThread& msgbox, quint32 exitCode)
{
    QString err = QString("TotalA.exe exited with error code 0x%1 (%2)\n").arg(exitCode, 8, 16, QChar('0')).arg(qint32(exitCode), 0, 10);
    err +=
        "\n"
        "Some common causes of issues are:\n"
        "- Error code -1? Check that TotalA.exe isn't already running\n"
        "- Error code 5, TAESC on Win7? Try delete aqrit.cfg/pdraw.dll\n"
        "- Low game resolution? Try 1024x768 or higher\n"
        "- Corrupted 3rd party .ufo files? If in doubt, clear them out\n"
        "- Missing TA_Features_2013.ccx? Many 3rd party maps need it\n"
        "- Incorrect installation? Carefully follow your mod's\n"
        "  instructions for your OS version (Win XP/8/9/10 etc)\n"
        "\n"
        "If the issue persists you might like to ask for help on any\n"
        "of the TA community's fine discord servers";

    QMetaObject::invokeMethod(&msgbox, "onMessage", Qt::QueuedConnection, Q_ARG(QString, "TAForever"), Q_ARG(QString, err), Q_ARG(unsigned int, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL));
}

int handleRegisterDplay(const QCoreApplication &app, const QCommandLineParser& parser)
{
    const char* ADDITIONAL_GAME_ARGS = "-c TAForever.ini";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_GUID = "{1336f32e-d116-4633-b853-4fee1ec91ea5}";

    QString dplayGuid = QUuid::createUuidV5(QUuid(DEFAULT_DPLAY_REGISTERED_GAME_GUID), parser.value("gamemod").toUpper()).toString();
    QString dplayAppName = "Total Annihilation Forever (" + parser.value("gamemod").toUpper() + ")";
    QString dplayGameArgs = ADDITIONAL_GAME_ARGS;
    if (!parser.value("gameargs").isEmpty() && !parser.value("gameargs").contains("-c", Qt::CaseInsensitive))
    {
        // only allow user to specify arguments if they're not trying to override the config file.  We want control of that ...
        dplayGameArgs = parser.value("gameargs") + ' ' + ADDITIONAL_GAME_ARGS;;
    }

    if (!QFileInfo(QDir(parser.value("gamepath")), parser.value("gameexe")).isExecutable())
    {
        QString err = QString("Unable to find ") + parser.value("gamemod").toUpper() + " at path \"" + parser.value("gamepath") + "\\" + parser.value("gameexe") + '"';
        err += "\nPlease check your game settings";
        MessageBox(
            NULL,
            err.toStdString().c_str(),
            "Unable to register game with DirectPlay",
            MB_OK | MB_ICONERROR | MB_SYSTEMMODAL
        );
        return 1;
    }

    if (parser.isSet("alreadyuac"))
    {
        RegisterDplayLobbyableApplication(
            dplayAppName, dplayGuid, parser.value("gamepath"), parser.value("gameexe"), dplayGameArgs, parser.value("gamepath"));
    }
    else if (!CheckDplayLobbyableApplication(
            dplayGuid, parser.value("gamepath"), parser.value("gameexe"), dplayGameArgs, parser.value("gamepath")))
    {
        QStringList args;
        args << "--registerdplay";
        args << "--alreadyuac";
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
                MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_SYSTEMMODAL
            );

            if (result == IDABORT)
                return 1;
            else if (result == IDIGNORE)
                break;

            RunAs(app.applicationFilePath(), args);
        }
    }
    return 0;
}


int doMain(int argc, char* argv[])
{
    const char* DEFAULT_DPLAY_REGISTERED_GAME_EXE = "totala.exe";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_PATH = "c:\\cavedog\\totala";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_ARGS = "";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_MOD = "TACC";

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("TALauncher");
    QCoreApplication::setApplicationVersion("0.14.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("Total Annihilation launch-to-lobby server");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("bindport", "Specifies port for LaunchServer to listen on", "bindport", "48684"));
    parser.addOption(QCommandLineOption("registerdplay", "Register the dplay lobbyable app with --gamepath, --gameexe, --gameargs. (requires run as admin)."));
    parser.addOption(QCommandLineOption("gamemod", "Name of the game variant (used to generate a DirectPlay registration that doesn't conflict with another variant.", "gamemod", DEFAULT_DPLAY_REGISTERED_GAME_MOD));
    parser.addOption(QCommandLineOption("gameargs", "Command line arguments for game executable. (required for --registerdplay).", "args", DEFAULT_DPLAY_REGISTERED_GAME_ARGS));
    parser.addOption(QCommandLineOption("gameexe", "Game executable. (required for --registerdplay).", "exe", DEFAULT_DPLAY_REGISTERED_GAME_EXE));
    parser.addOption(QCommandLineOption("gamepath", "Path from which to launch game. (required for --registerdplay).", "path", DEFAULT_DPLAY_REGISTERED_GAME_PATH));
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs.", "logfile", ""));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug).", "logfile", "5"));
    parser.addOption(QCommandLineOption("uac", "request to elevate to admin."));
    parser.addOption(QCommandLineOption("alreadyuac", "indicate that app is running with admin privileges already."));
    parser.addOption(QCommandLineOption("keepalivetimeout", "Number of seconds to stay alive without receiving keep alive messages over console.", "keepalivetimeout", "10"));
    parser.process(app);

    if (parser.isSet("uac") && !parser.isSet("alreadyuac"))
    {
        QStringList args;
        for (const char* arg : { "gamepath", "gameexe", "gameargs", "gamemod", "bindaddress", "bindport", "logfile", "loglevel" })
        {
            if (parser.isSet(arg))
            {
                args << (QString("--") + arg) << parser.value(arg);
            }
        }
        for (const char* arg : { "registerdplay" })
        {
            if (parser.isSet(arg))
            {
                args << QString("--") + arg;
            }
        }
        args << "--alreadyuac";

        RunAs(app.applicationFilePath(), args);
        return 0;
    }

    if (parser.isSet("registerdplay"))
    {
        return handleRegisterDplay(app, parser);
    }

    taflib::Logger::Initialise(parser.value("logfile").toStdString(), taflib::Logger::Verbosity(parser.value("loglevel").toInt()));
    qInstallMessageHandler(taflib::Logger::Log);

    talaunch::LaunchServer launchServer(QHostAddress("127.0.0.1"), parser.value("bindport").toInt(), parser.value("keepalivetimeout").toInt());
    taflib::MessageBoxThread msgbox;
    QObject::connect(&launchServer, &talaunch::LaunchServer::quit, &app, &QCoreApplication::quit);
    QObject::connect(&launchServer, &talaunch::LaunchServer::gameFailedToLaunch, [&msgbox](QString guid) {
        UnableToLaunchMsgBox(msgbox, guid);
    });
    QObject::connect(&launchServer, &talaunch::LaunchServer::gameExitedWithError, [&msgbox](quint32 exitCode) {
        GameExitedWithErrorMsgBox(msgbox, exitCode);
    });
    return app.exec();
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
