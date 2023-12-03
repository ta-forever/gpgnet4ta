#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qcryptographichash.h>
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
#include "taflib/Logger.h"
#include "taflib/HexDump.h"
#include "tapacket/TADemoWriter.h"
#include "tareplay/TaDemoCompilerMessages.h"
#include "tareplay/TaReplayServer.h"
#include "tareplay/TaDemoCompiler.h"

#include "TafLobbyClient.h"
#include "TafHwIdGenerator.h"
#include "VersionString.h"

using namespace tareplay;

int doMain(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("ReplayServer");
    QCoreApplication::setApplicationVersion(VERSION_STRING);

    QCommandLineParser parser;
    parser.setApplicationDescription("TA Replay Server");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs.", "logfile", ""));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug).", "logfile", "5"));
    parser.addOption(QCommandLineOption("demofile", "template for file path in which to save demo files.", "demopath", "taf-game-%1.tad"));
    parser.addOption(QCommandLineOption("addr", "interface address to listen for demo data.", "addr"));
    parser.addOption(QCommandLineOption("port", "port on which to listen for demo data.", "port", "15000"));
    parser.addOption(QCommandLineOption("livedelaysecs", "Number of seconds to delay the live replay by", "livedelaysecs", "300"));
    parser.addOption(QCommandLineOption("maxsendrate", "Maximum bytes per user per second to send replay data. (1hr, 8 player ESC game ~20MB)", "maxsendrate", "30000"));
    parser.addOption(QCommandLineOption("mindemosize", "Discard demos smaller than this number of bytes", "mindemosize", "100000"));
    parser.addOption(QCommandLineOption("compiler", "run the TA Demo Compiler Server"));
    parser.addOption(QCommandLineOption("replayer", "run the TA Demo Replay Server"));
    parser.addOption(QCommandLineOption("lobbyserver", "Connect to lobby server to retrieve game information eg lobby.taforever.com:8001", "lobbyserver", ""));
    parser.addOption(QCommandLineOption("nousercontextoption", "What to do when message received for a player with insufficient context: ignore, close, abort or disconnect", "nousercontextoption", "ignore"));
    parser.process(app);

    taflib::Logger::Initialise(parser.value("logfile").toStdString(), taflib::Logger::Verbosity(parser.value("loglevel").toInt()));
    qInstallMessageHandler(taflib::Logger::Log);

    TafLobbyClient tafLobbyClient(QCoreApplication::applicationName(), QCoreApplication::applicationVersion());
    if (!parser.value("lobbyserver").isEmpty() && qEnvironmentVariableIsSet("RS_USERNAME") && qEnvironmentVariableIsSet("RS_PASSWORD"))
    {
        QStringList hostAndPort = parser.value("lobbyserver").split(':');
        tafLobbyClient.connectToHost(hostAndPort[0], hostAndPort[1].toInt());
        QObject::connect(&tafLobbyClient, &TafLobbyClient::session, [&tafLobbyClient](quint64 sessionId) {
            QString uid = "0"; // TafHwIdGenerator("C:/Program Files/TA Forever Client/natives/faf-uid.exe").get(sessionId);
            QString username = qgetenv("RS_USERNAME");
            QString password = qgetenv("RS_PASSWORD");
            password = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex();
            tafLobbyClient.sendHello(sessionId, uid, "0.0.0.0", username, password);
        });
    }

    TaDemoCompiler::NoUserContextOption noUserContextOption;
    if (parser.value("nousercontextoption") == "close")
    {
        noUserContextOption = TaDemoCompiler::NoUserContextOption::CLOSE_CONNECTION;
        qInfo() << "NoUserContextOption: CLOSE";
    }
    else if (parser.value("nousercontextoption") == "abort")
    {
        noUserContextOption = TaDemoCompiler::NoUserContextOption::ABORT_CONNECTION;
        qInfo() << "NoUserContextOption: ABORT";
    }
    else if (parser.value("nousercontextoption") == "disconnect")
    {
        noUserContextOption = TaDemoCompiler::NoUserContextOption::DISCONNECT_FROM_HOST;
        qInfo() << "NoUserContextOption: DISCONNECT";
    }
    else
    {
        noUserContextOption = TaDemoCompiler::NoUserContextOption::IGNORE;
        qInfo() << "NoUserContextOption: IGNORE";
    }

    std::shared_ptr<TaDemoCompiler> compiler;
    std::shared_ptr<TaReplayServer> replayServer;
    QHostAddress host = parser.isSet("addr") ? QHostAddress(parser.value("addr")) : QHostAddress(QHostAddress::AnyIPv4);
    quint16 port = parser.value("port").toInt();
    if (parser.isSet("compiler"))
    {
        compiler.reset(new TaDemoCompiler(parser.value("demofile"), host, port++, parser.value("mindemosize").toUInt(), noUserContextOption));
    }
    if (parser.isSet("replayer"))
    {
        replayServer.reset(new TaReplayServer(
            parser.value("demofile"),
            host,
            port++,
            parser.value("livedelaysecs").toUInt(),
            parser.value("maxsendrate").toInt()
        ));

        QObject::connect(&tafLobbyClient, &TafLobbyClient::gameInfo, [replayServer](TafLobbyGameInfo gameInfo) {
            replayServer->setGameInfo(gameInfo.id, gameInfo.replayDelaySeconds, gameInfo.state);
        });
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
