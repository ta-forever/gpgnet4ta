/*	Copyright 2021 Alexander Lowe (loweam@gmail.com)

This file is part of JDPlay.

JDPlay is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDPlay is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDPlay.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtCore/qbytearray.h>
#include <QtCore/qdatastream.h>
#include <QtCore/qsettings.h>
#include <QtCore/qfile.h>
#include <QtCore/qrunnable.h>
#include <QtCore/qthread.h>
#include <QtCore/qdir.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>

#include "jdplay/JDPlay.h"
#include "tademo/TADemoParser.h"
#include "tademo/GameMonitor.h"

struct CreateLobbyCommand
{
    int protocol;
    int localPort;
    QString playerName;
    int playerId;
    int natTraversal;

    CreateLobbyCommand() :
        protocol(0),
        localPort(47625),
        playerName("BILLYIDOL"),
        playerId(1955),
        natTraversal(1)
    { }

    CreateLobbyCommand(QVariantList command)
    {
        Set(command);
    }

    void Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("CreateLobby"))
        {
            throw std::runtime_error("Unexpected command");
        }
        protocol = command[1].toInt();
        localPort = command[2].toInt();
        playerName = command[3].toString();
        playerId = command[4].toInt();
        natTraversal = command[5].toInt();
    }
};

struct HostGameCommand
{
    QString mapName;

    HostGameCommand() : 
        mapName("SHERWOOD")
    { }

    HostGameCommand(QVariantList qvl)
    {
        Set(qvl);
    }

    void Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("HostGame"))
        {
            throw std::runtime_error("Unexpected command");
        }
        mapName = command[1].toString();
    }
};

struct JoinGameCommand
{
    QString hostAndPort;
    QString remotePlayerName;
    int remotePlayerId;

    JoinGameCommand() :
        hostAndPort("127.0.0.1:47625"),
        remotePlayerName("ACDC"),
        remotePlayerId(1973)
    { }

    JoinGameCommand(QVariantList qvl)
    {
        Set(qvl);
    }

    void Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("JoinGame"))
        {
            throw std::runtime_error("Unexpected command");
        }
        hostAndPort = command[1].toString();
        remotePlayerName = command[2].toString();
        remotePlayerId = command[3].toInt();

        QStringList playerParts = remotePlayerName.split("@");
        if (playerParts.size() == 2)
        {
            remotePlayerName = playerParts[0];
            hostAndPort = playerParts[1];
        }
    }
};

class GpgNetReceive
{
    QDataStream &m_is;

    static std::uint8_t GetByte(QDataStream &is)
    {
        std::uint8_t byte;
        is.readRawData((char*)&byte, 1);
        return byte;
    }

    static std::uint32_t GetInt(QDataStream &is)
    {
        std::uint32_t word;
        is.readRawData((char*)&word, 4);
        return word;
    }

    static QString GetString(QDataStream &is)
    {
        std::uint32_t size = GetInt(is);
        QSharedPointer<char> buffer(new char[1+size]);
        is.readRawData(buffer.data(), size);
        buffer.data()[size] = 0;
        return QString(buffer.data());
    }

public:
    GpgNetReceive(QDataStream &is) :
        m_is(is)
    { }

    QVariantList GetCommand()
    {
        return GetCommand(m_is);
    }

    static QVariantList GetCommand(QDataStream &is)
    {
        QVariantList commandAndArgs;

        QString command = GetString(is);
        std::uint32_t numArgs = GetInt(is);
        commandAndArgs.append(command);

        for (int n = 0; n < numArgs; ++n)
        {
            std::uint8_t argType = GetByte(is);
            if (argType == 0)
            {
                std::uint32_t arg = GetInt(is);
                commandAndArgs.append(arg);
            }
            else if (argType == 1)
            {
                QString arg = GetString(is);
                commandAndArgs.append(arg);
            }
            else
            {
                throw std::runtime_error("unexpected argument type");
            }
        }
        return commandAndArgs;
    }
};

class GpgNetSend
{
    QDataStream &m_os;

    void SendCommand(QString command, int argumentCount)
    {
        qDebug() << "GpgNetSend command" << command;
        m_os << command.toUtf8() << quint32(argumentCount);
    }

    void SendArgument(QString arg)
    {
        qDebug() << "GpgNetSend arg" << arg;
        m_os << quint8(1) << arg.toUtf8();
    }

    void SendArgument(int arg)
    {

        qDebug() << "GpgNetSend arg" << arg;
        m_os << quint8(0) << quint32(arg);
    }

public:

    GpgNetSend(QDataStream &os) :
        m_os(os)
    { }

    void gameState(QString state)
    {
        SendCommand("GameState", 1);
        SendArgument(state);
    }

    void createLobby(int /* eg 0 */, int /* eg 0xb254 */, const char *playerName, int /* eg 0x9195 */, int /* eg 1 */)
    {
        throw std::runtime_error("not implemented");
    }

    void hostGame(QString mapName)
    {
        SendCommand("HostGame", 1);
        SendArgument(mapName);
    }

    void joinGame(QString hostAndPort, QString remotePlayerName, int remotePlayerId)
    {
        SendCommand("JoinGame", 3);
        SendArgument(remotePlayerName);
        SendArgument(remotePlayerId);
    }

    void gameMods(int numMods)
    {
        SendCommand("GameMods", 2);
        SendArgument("activated");
        SendArgument(numMods);
    }

    void gameMods(QStringList uids)
    {
        SendCommand("GameMods", 2);
        SendArgument("uids");
        SendArgument(uids.join(' '));
    }

    void gameOption(QString key, QString value)
    {
        SendCommand("GameOption", 2);
        SendArgument(key);
        SendArgument(value);
    }

    void gameOption(QString key, int value)
    {
        SendCommand("GameOption", 2);
        SendArgument(key);
        SendArgument(value);
    }

    void playerOption(QString playerId, QString key, QString value)
    {
        SendCommand("PlayerOption", 3);
        SendArgument(playerId);
        SendArgument(key);
        SendArgument(value);
    }

    void playerOption(QString playerId, QString key, int value)
    {
        SendCommand("PlayerOption", 3);
        SendArgument(playerId);
        SendArgument(key);
        SendArgument(value);
    }

    void aiOption(QString key, QString value)
    {
        SendCommand("AIOption", 2);
        SendArgument(key);
        SendArgument(value);
    }

    void clearSlot(int slot)
    {
        SendCommand("ClearSlot", 1);
        SendArgument(slot);
    }

};


QString buildPlayerName(QString baseName, int mean, int deviation, int numgames, QString country)
{
    QString playerName = baseName;
    if (country.size() > 0)
    {
        playerName += QString("-%1").arg(country);
    }
    if (numgames > 10)
    {
        playerName += QString("-%1").arg(mean - 2 * deviation);
    }
    return playerName;
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

void CreateTAInitFile(QString tmplateFilename, QString iniFilename, QString session, QString mission, int playerLimit, bool lockOptions)
{
    qDebug() << "Loading ta ini template:" << tmplateFilename;
    QFile tmplt(tmplateFilename);
    if (!tmplt.open(QFile::ReadOnly | QFile::Text))
    {
        return;
    }

    QTextStream in(&tmplt);
    QString txt = in.readAll();
    txt.replace("{session}", session);
    txt.replace("{mission}", mission);
    txt.replace("{playerlimit}", QString::number(max(2, min(playerLimit, 10))));
    txt.replace("{lockoptions}", lockOptions ? "1" : "0");

    qDebug() << "saving ta ini:" << iniFilename;
    QFile ini(iniFilename);
    if (!ini.open(QFile::WriteOnly | QFile::Text))
    {
        return;
    }

    QTextStream out(&ini);
    ini.write(txt.toUtf8());
}




static void HexDump(const char *_buff, std::size_t size)
{
    const unsigned char *buff = (const unsigned char*)_buff;
    for (std::size_t base = 0; base < size; base += 16)
    {
        std::cout << std::setw(4) << std::hex << base << ": ";
        for (std::size_t ofs = 0; ofs < 16; ++ofs)
        {
            std::size_t idx = base + ofs;
            if (idx < size)
            {
                unsigned byte = buff[idx] & 0x0ff;
                std::cout << std::setw(2) << std::hex << byte << ' ';
            }
            else
            {
                std::cout << "   ";
            }
        }
        for (std::size_t ofs = 0; ofs < 16; ++ofs)
        {
            std::size_t idx = base + ofs;
            if (idx < size && std::isprint(buff[idx]))
            {
                std::cout << buff[idx];
            }
            else
            {
                std::cout << " ";
            }
        }
        std::cout << std::endl;
    }
}

QStringList GetPossibleTADemoSaveDirs()
{
    QStringList result;

    for (const char *organisation : { "Yankspankers", "TA Patch", "TA Esc" })
    {
        QSettings registry(QSettings::NativeFormat, QSettings::UserScope, organisation, "TA Demo");
        QString defdir = registry.value("Options/defdir").toString();
        defdir = defdir.replace("\\\\", "\\");
        if (defdir.size() == 0)
        {
            continue;
        }
        if (*defdir.rbegin() == "\\")
        {
            defdir = defdir.mid(0, defdir.size() - 1);
        }
        if (defdir.size()>0 && QDir(defdir).exists())
        {
            qDebug() << organisation << defdir;
            result.append(defdir);
        }
    }
    return result;
}

QStringList GetTaDemoFiles(QStringList taDemoDirs)
{
    QStringList demos;
    for (QString dir : taDemoDirs)
    {
        QStringList tadFiles = QDir(dir).entryList(QStringList() << "*.tad" << "*.ted", QDir::Files);
        std::transform(tadFiles.begin(), tadFiles.end(), tadFiles.begin(), [dir](QString fn) -> QString { return dir + "\\" + fn; });
        demos += tadFiles;
    }
    return demos;
}

// return c=a-b, elements of a not in b
QStringList StringListDiff(QStringList a, QStringList b)
{
    QStringList c;

    std::set<QString> bset(b.begin(), b.end());
    for (QString ai : a)
    {
        if (!bset.count(ai))
        {
            c.append(ai);
        }
    }
    return c;
}

void RunGpgNet(QCoreApplication *app, QString iniTemplate, QString iniTarget, QString host,
    double mean, double deviation, QString country, int numGames, QString guid, int playerLimit, bool lockOptions)
{
    QTcpSocket socket(app);
    {
        qDebug() << "Connecting to GPGNet " << host;
        QStringList hostAndPort = host.split(QRegExp("\\:"));
        socket.connectToHost(hostAndPort[0], hostAndPort[1].toInt());

        if (!socket.waitForConnected(10000))
        {
            qDebug() << "Unable to connect";
            return;
        }
    }
    qDebug() << "Connected";

    QDataStream ds(&socket);
    ds.setByteOrder(QDataStream::LittleEndian);
    GpgNetSend gpgSend(ds);

    QString gameState;
    CreateLobbyCommand createLobbyCommand;
    JDPlay jdplay(createLobbyCommand.playerName.toStdString().c_str(), 0, false);

    QStringList taDemoPaths = GetPossibleTADemoSaveDirs();
    QStringList oldTaDemos;
    GameMonitor taDemoMonitor;
    std::shared_ptr<std::istream> taDemoStream;

    gpgSend.gameState(gameState = "Idle");
    while (socket.isOpen())
    {
        QVariantList serverCommand;
        QString cmd;
        if (socket.bytesAvailable() > 0)
        //if (socket.waitForReadyRead(1000))
        {
            serverCommand = GpgNetReceive::GetCommand(ds);
            cmd = serverCommand[0].toString();
        }

        if (gameState == "Idle" && !cmd.compare("CreateLobby"))
        {
            createLobbyCommand.Set(serverCommand);
            QString playerName = buildPlayerName(
                createLobbyCommand.playerName, mean, deviation, numGames, country);
            qDebug() << "CreateLobby(playerName:" << playerName << ")";
            jdplay.updatePlayerName(playerName.toStdString().c_str());
            gpgSend.gameState(gameState = "Lobby");
            continue;
        }
        else if (gameState == "Lobby" && !cmd.compare("HostGame"))
        {
            HostGameCommand hgc(serverCommand);
            QString sessionName = createLobbyCommand.playerName + "'s Game";
            CreateTAInitFile(iniTemplate, iniTarget, sessionName, hgc.mapName, playerLimit, lockOptions);
            bool ret = jdplay.initialize(guid.toStdString().c_str(), "0.0.0.0", true, 10);
            if (!ret)
            {
                qDebug() << "unable to initialise dplay";
                return;
            }
            qDebug() << "HostGame(guid=" << guid << ", mapName=" << hgc.mapName << ")";
            ret = jdplay.launch(true);
            if (!ret)
            {
                qDebug() << "unable to launch game";
                return;
            }

            gpgSend.gameOption("Slots", 10);
            QString playerName = createLobbyCommand.playerName;
            gpgSend.playerOption(playerName, "Color", 1);

            gameState = "Hosted";
            oldTaDemos = GetTaDemoFiles(taDemoPaths);
            continue;
        }
        else if (gameState == "Lobby" && !cmd.compare("JoinGame"))
        {
            JoinGameCommand jgc(serverCommand);
            qDebug() << "JoinGame(guid = " << guid << ", hostip = " << jgc.hostAndPort << ")";

            QStringList candidateHostIps = jgc.hostAndPort.split(":")[0].split(";");
            Q_FOREACH(QString _hostip, candidateHostIps)
            {
                char hostip[257] = { 0 };
                std::strncpy(hostip, _hostip.toStdString().c_str(), 256);

                qDebug() << "searching at hostip" << hostip << "...";
                bool ret = jdplay.initialize(guid.toStdString().c_str(), hostip, false, 10);
                if (!ret)
                {
                    qDebug() << "unable to initialise dplay";
                    return;
                }

                bool sessionFound = false;
                for (int n = 0; n < 3 && !sessionFound; ++n)
                {
                    sessionFound = jdplay.searchOnce();
                }
                if (!sessionFound)
                {
                    continue;
                }

                qDebug() << "game found.  Launching ...";
                ret = jdplay.launch(true);
                jdplay.releaseDirectPlay();

                QString playerName = createLobbyCommand.playerName;
                gpgSend.playerOption(playerName, "Color", 2);

                gameState = "Joined";
                break;
            }
            if (gameState != "Joined")
            {
                qDebug() << "unable to find game at any candidate hosts ... quitting";
                return;
            }
            continue;
        }
        else if (gameState == "Hosted")
        {
            bool active = jdplay.pollStillActive();
            if (active)
            {
                jdplay.pollSessionStatus();
                jdplay.printSessionDesc();
                qDebug() << "-----";

                int numPlayers = jdplay.getUserData1() >> 16 & 0x0f;
                bool closed = jdplay.getUserData1() & 0x80000000;
                std::string mapName = jdplay.getAdvertisedSessionName();
                qDebug() << "mapName" << mapName.c_str();
                if (mapName.size() > 16)
                {
                    mapName = mapName.substr(16);
                    gpgSend.gameOption("MapName", QString::fromStdString(mapName));
                }

                if (!taDemoStream)
                {
                    qDebug() << "taDemoPaths" << taDemoPaths;
                    qDebug() << "oldTaDemos" << oldTaDemos;
                    QStringList newTaDemoFiles = StringListDiff(GetTaDemoFiles(taDemoPaths), oldTaDemos);
                    qDebug() << "newTaDemoFiles" << newTaDemoFiles;
                    if (newTaDemoFiles.size() > 0)
                    {
                        taDemoStream.reset(new std::ifstream(newTaDemoFiles.at(0).toStdString(), std::ios::in | std::ios::binary));
                    }
                }
                if (taDemoStream)
                {
                    qDebug() << "taDemoStream mapname:" << taDemoMonitor.getMapName().c_str();
                    taDemoMonitor.parse(taDemoStream.get());
                    if (taDemoMonitor.isGameStarted() && !taDemoMonitor.isGameOver())
                    {
                        gpgSend.gameOption("MapName", QString::fromStdString(taDemoMonitor.getMapName()));
                        gpgSend.gameState(gameState = "Launching");
                    }
                }
            }
            else
            {
                jdplay.releaseDirectPlay();
                gpgSend.gameState(gameState = "Ended");
                return;
            }
        }
        else if (gameState == "Joined" || gameState == "Launching")
        {
            bool active = jdplay.pollStillActive();
            if (!active)
            {
                gpgSend.gameState(gameState = "Ended");
                return;
            }
        }
        else
        {
            qDebug() << "ignored state/command: gameState=" << gameState << "cmd=" << cmd;
        }

        socket.waitForReadyRead(3000);
    }
}


int main(int argc, char* argv[])
{
    const char *DEFAULT_GAME_INI_TEMPLATE = "TAForever.ini.template";
    const char *DEFAULT_GAME_INI = "TAForever.ini";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_GUID = "{1336f32e-d116-4633-b853-4fee1ec91ea5}";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_EXE = "totala.exe";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_PATH = "c:\\cavedog\\totala";
    const char *DEFAULT_DPLAY_REGISTERED_GAME_ARGS = "-c TAForever.ini";

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("GpgPlay");
    QCoreApplication::setApplicationVersion("1.0");

    // GpgPlay.exe /gamepath d:\games\ta /gameexe totala.exe /gameargs "-d -c d:\games\taforever.ini" /gpgnet 127.0.0.1:37135 /mean 1500 /deviation 75 /savereplay gpgnet://127.0.0.1:50703/12797031/Axle.SCFAreplay /country AU /numgames 878
    QCommandLineParser parser;
    parser.setApplicationDescription("GPGNet facade for Direct Play games");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("guid", "GUID of DirectPlay registration.", "guid", DEFAULT_DPLAY_REGISTERED_GAME_GUID));
    parser.addOption(QCommandLineOption("gpgnet", "Uri to GPGNet.", "host:port"));
    parser.addOption(QCommandLineOption("mean", "Player rating mean.", "mean"));
    parser.addOption(QCommandLineOption("deviation", "Player rating deviation.", "deviation"));
    parser.addOption(QCommandLineOption("country", "Player country code.", "code"));
    parser.addOption(QCommandLineOption("numgames", "Player game count.", "count"));
    parser.addOption(QCommandLineOption("players", "Max number of players 2 to 10", "players", "10"));
    parser.addOption(QCommandLineOption("lockoptions", "Lock (some of) the lobby options"));
    parser.addOption(QCommandLineOption("registerdplay", "Register the dplay lobbyable app with --guid, --gamepath, --gameexe, --gameargs. (requires run as admin)."));
    parser.addOption(QCommandLineOption("gamepath", "Path from which to launch game. (required for --registerdplay).", "path", DEFAULT_DPLAY_REGISTERED_GAME_PATH));
    parser.addOption(QCommandLineOption("gameexe", "Game executable. (required for --registerdplay).", "exe", DEFAULT_DPLAY_REGISTERED_GAME_EXE));
    parser.addOption(QCommandLineOption("gameargs", "Command line arguments for game executable. (required for --registerdplay).", "args", DEFAULT_DPLAY_REGISTERED_GAME_ARGS));
    parser.addOption(QCommandLineOption("testlaunch", "Launch TA straight away."));
    parser.process(app);

    if (parser.isSet("registerdplay"))
    {
        RegisterDplayLobbyableApplication(
            "Total Annihilation Forever",
            parser.value("guid"),
            parser.value("gamepath"),
            parser.value("gameexe"),
            parser.value("gameargs"),
            parser.value("gamepath"));
    }

    // not from the command line, from the dplay registry since thats what we're actually going to launch
    QString gamePath = GetDplayLobbableAppPath(parser.value("guid"), parser.value("gamepath"));
    qDebug() << "guid:" << parser.value("guid");
    qDebug() << "game path:" << gamePath;

    if (parser.isSet("testlaunch"))
    {
        CreateLobbyCommand createLobbyCommand;
        JDPlay jdplay(createLobbyCommand.playerName.toStdString().c_str(), 3, false);
        bool ret = jdplay.initialize(parser.value("guid").toStdString().c_str(), "0.0.0.0", true, 10);
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
    }

    if (parser.isSet("gpgnet"))
    {
        RunGpgNet(
            &app,
            DEFAULT_GAME_INI_TEMPLATE,
            gamePath + "\\" + DEFAULT_GAME_INI,
            parser.value("gpgnet"),
            parser.value("mean").toDouble(), parser.value("deviation").toDouble(),
            parser.value("country"), parser.value("numgames").toInt(),
            parser.value("guid"),
            parser.value("players").toInt(),
            parser.isSet("lockoptions"));
    }

}
