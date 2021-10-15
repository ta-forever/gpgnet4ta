#include "TaReplayer.h"

#include "taflib/Logger.h"
#include "taflib/HexDump.h"
#include "taflib/MessageBoxThread.h"
#include "tapacket/UnitDataRepo.h"
#include "tapacket/TADemoParser.h"
#include "talaunch/LaunchClient.h"
#include "tareplay/TaReplayClient.h"

#include <QtCore/qcommandlineparser.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qcryptographichash.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qurl.h>
#include <QtCore/quuid.h>
#include <QtCore/qtimer.h>

#include <iostream>
#include <sstream>
#include <windows.h>

void GameNotFoundMessageBox(taflib::MessageBoxThread& msgbox)
{
    QString err = "TAF Replay Server responded GAME NOT FOUND. This is typically because the game includes AI players. TAF suggests you find a different game to watch, or go play one yourself.";
    QMetaObject::invokeMethod(&msgbox, "onMessage", Qt::QueuedConnection, Q_ARG(QString, "TAForever"), Q_ARG(QString, err), Q_ARG(unsigned int, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL));
}

void LiveReplayDisabledMessageBox(taflib::MessageBoxThread& msgbox)
{
    QString err = "TAF Replay Server responded LIVE REPLAY DISABLED. The host has disabled live replay for this game because their underpants are too tight. TAF suggests you find a different game to watch, or go play one yourself.";
    QMetaObject::invokeMethod(&msgbox, "onMessage", Qt::QueuedConnection, Q_ARG(QString, "TAForever"), Q_ARG(QString, err), Q_ARG(unsigned int, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL));
}

class DemoInfoParser : public tapacket::DemoParser
{
public:
    DemoInfoParser():
        m_mapHash(0u)
    { }

    virtual void handle(const tapacket::Header& header)
    {
        m_mapName = QString::fromStdString(header.mapName);
    }

    virtual void handle(const tapacket::Player& player, int n, int ofTotal)
    { }

    virtual void handle(const tapacket::ExtraSector& es, int n, int ofTotal)
    { }

    virtual void handle(const tapacket::PlayerStatusMessage& msg, std::uint32_t dplayid, int n, int ofTotal)
    {
        tapacket::TPlayerInfo playerInfo(msg.statusMessage);
        if (playerInfo.getMapName() == m_mapName.toStdString())
        {
            m_mapHash = playerInfo.getMapHash();
            m_versionMajor = playerInfo.versionMajor;
            m_versionMinor = playerInfo.versionMinor;
        }
    }

    virtual void handle(const tapacket::UnitData& unitData)
    {
        const std::uint8_t* ptr = unitData.unitData.data();
        const std::uint8_t* end = ptr + unitData.unitData.size();
        unsigned subpakLen = tapacket::TPacket::getExpectedSubPacketSize(ptr, end - ptr);
        while (ptr < end && subpakLen > 0u)
        {
            tapacket::bytestring bs(ptr, subpakLen);
            m_units.add(bs);
            ptr += subpakLen;
        }
    }

    virtual void handle(const tapacket::Packet& packet, const std::vector<tapacket::bytestring>& unpaked, std::size_t n)
    { }

    QByteArray getUnitDataFingerPrint()
    {
        QCryptographicHash md5(QCryptographicHash::Md5);
        m_units.hash([&md5](std::uint32_t datum) {
            md5.addData((const char*)&datum, sizeof(datum));
        });
        return md5.result();
    }

    QJsonObject toJson(QString filePath)
    {
        QJsonObject json;
        json.insert("filePath", filePath);
        json.insert("mapName", m_mapName);
        json.insert("mapHash", QString("%1").arg(m_mapHash, 8, 16, QChar('0')));
        json.insert("modHash", QString(getUnitDataFingerPrint().toHex()));
        json.insert("taVersionMajor", m_versionMajor);
        json.insert("taVersionMinor", m_versionMinor);
        return json;
    }

private:
    tapacket::UnitDataRepo m_units;
    QString m_mapName;
    quint32 m_mapHash;
    quint8 m_versionMajor;
    quint8 m_versionMinor;
};

int doMain(int argc, char* argv[])
{
    const char* OTA_DPLAY_REGISTERED_GAME_GUID = "{99797420-F5F5-11CF-9827-00A0241496C8}";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_GUID = "{1336f32e-d116-4633-b853-4fee1ec91ea5}";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_PATH = "c:\\cavedog\\totala";

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("TaReplayer");
    QCoreApplication::setApplicationVersion("0.14.4");

    QCommandLineParser parser;
    parser.setApplicationDescription("TA Replayer");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs.", "logfile", ""));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug).", "logfile", "5"));
    parser.addOption(QCommandLineOption("demourl", "gpgnet://host:port/gameid where host:port is the Replay Server.  Or file://TAG_ROCK_loses_on_GoW_again.tad to replay a local file", "demourl", "gpgnet://taforever.com:15000/1234"));
    parser.addOption(QCommandLineOption("gamemod", "Which TA mod to launch.", "gamemod"));
    parser.addOption(QCommandLineOption("playername", "What does the watcher want to call him/herself?", "playername", "BILLY_IDOL"));
    parser.addOption(QCommandLineOption("launchserverport", "Specifies port that LaunchServer is listening on", "launchserverport"));
    parser.addOption(QCommandLineOption("info", "Just print out some info about the demo and exit"));
    parser.process(app);

    if (parser.isSet("info"))
    {
        QUrl demoUrl = QUrl::fromUserInput(parser.value("demourl"));
        if (!demoUrl.isValid())
        {
            throw std::runtime_error("[doMain] invalid --demourl '" + parser.value("demourl").toStdString() + "'");
        }
        else if (!demoUrl.isLocalFile())
        {
            throw std::runtime_error("[doMain] invalid --demourl '" + parser.value("demourl").toStdString() + "'.  Must be a local file!");
        }
        else
        {

            QFileInfo fileInfo(demoUrl.toLocalFile());
            if (!fileInfo.exists() || !fileInfo.isFile())
            {
                throw std::runtime_error("[doMain] file not found '" + demoUrl.toLocalFile().toStdString() + "'");
            }
            QByteArray localFile = QFile::encodeName(demoUrl.toLocalFile());
            localFile += '\0';
            std::ifstream ifs(localFile.data(), std::ios::in | std::ios::binary);
            if (!ifs.good())
            {
                throw std::runtime_error(std::string("[doMain] error reading file '") + localFile.data() + "'");
            }
            DemoInfoParser demoInfoParser;
            demoInfoParser.parse(&ifs, 100);
            std::cout << QString(QJsonDocument(demoInfoParser.toJson(localFile.data())).toJson()).toStdString() << std::endl;
            return 0;
        }
    }

    taflib::Logger::Initialise(parser.value("logfile").toStdString(), taflib::Logger::Verbosity(parser.value("loglevel").toInt()));
    qInstallMessageHandler(taflib::Logger::Log);

    QString dplayGuid = OTA_DPLAY_REGISTERED_GAME_GUID;
    if (parser.isSet("gamemod"))
    {
        dplayGuid = QUuid::createUuidV5(QUuid(DEFAULT_DPLAY_REGISTERED_GAME_GUID), parser.value("gamemod").toUpper()).toString();
    }

    std::shared_ptr<talaunch::LaunchClient> launchClient;
    std::shared_ptr<std::istream> demoStream;
    std::shared_ptr<tareplay::TaReplayClient> replayClient;
    std::shared_ptr<Replayer> replayer;
    taflib::MessageBoxThread msgbox;

    if (parser.isSet("launchserverport"))
    {
        launchClient.reset(new talaunch::LaunchClient(QHostAddress("127.0.0.1"), parser.value("launchserverport").toInt()));
        launchClient->setPlayerName(parser.value("playername"));
        launchClient->setGameGuid(dplayGuid);
        launchClient->setAddress("127.0.0.1");
        launchClient->setIsHost(false);
        launchClient->setRequireSearch(true);
    }

    QUrl demoUrl = QUrl::fromUserInput(parser.value("demourl"));
    if (!demoUrl.isValid())
    {
        throw std::runtime_error("[doMain] invalid --demourl '" + parser.value("demourl").toStdString() + "'");
    }
    else if (demoUrl.isLocalFile())
    {
        qInfo() << "Loading demo file:" << demoUrl.toLocalFile();
        QFileInfo fileInfo(demoUrl.toLocalFile());
        if (!fileInfo.exists() || !fileInfo.isFile())
        {
            throw std::runtime_error("[doMain] file not found '" + demoUrl.toString().toStdString() + "'");
        }
        QByteArray localFile = QFile::encodeName(demoUrl.toLocalFile());
        localFile += '\0';
        demoStream.reset(new std::ifstream(localFile.data(), std::ios::in | std::ios::binary));
        replayer.reset(new Replayer(demoStream.get()));
    }
    else
    {
        QString serverHostName = demoUrl.host();
        int port = demoUrl.port();
        int gameId = demoUrl.path().replace("/", "").toInt();
        qInfo() << "[doMain] connecting to replay server addr,port,gameid" << serverHostName << port << gameId;
        replayClient.reset(new tareplay::TaReplayClient(serverHostName, port, gameId, 0));
        replayer.reset(new Replayer(replayClient->getReplayStream()));
        QObject::connect(replayClient.get(), &tareplay::TaReplayClient::gameNotFound, [&app, &msgbox](tareplay::TaReplayServerStatus status) {
            QObject::connect(&msgbox, &taflib::MessageBoxThread::userAcknowledged, &app, QCoreApplication::quit);
            if (status == tareplay::TaReplayServerStatus::GAME_NOT_FOUND) GameNotFoundMessageBox(msgbox);
            if (status == tareplay::TaReplayServerStatus::LIVE_REPLAY_DISABLED) LiveReplayDisabledMessageBox(msgbox);
        });
    }


    QTimer timer;
    if (launchClient)
    {
        QObject::connect(&timer, &QTimer::timeout, [&launchClient, &demoStream, &replayClient, &replayer, &app]() {
            if (!launchClient->isRunning())
            {
                qInfo() << "[doMain] TA not running.  shutting down replayer";
                demoStream.reset();
                replayClient.reset();
                replayer.reset();
                app.quit();
            }
        });

        QObject::connect(replayer.get(), &Replayer::readyToJoin, [&launchClient, &timer]() {
            qInfo() << "[doMain] launching TA";
            launchClient->launch();
            timer.setInterval(1000);
            timer.start();
        });
    }

    replayer->hostGame(dplayGuid, "TAF Replayer", "127.0.0.1");

    app.exec();
    return 0;
}

#include <QtNetwork/qnetworkreply.h>

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
