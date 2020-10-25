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
#include <QtCore/qdatastream.h>
#include <QtCore/qsettings.h>
#include <QtCore/qfile.h>
#include <QtCore/qdir.h>
#include <QtCore/qthread.h>

#include <algorithm>
#include <fstream>

#include "jdplay/JDPlay.h"
#include "gpgnet/GpgNetReceive.h"
#include "gpgnet/GpgNetSend.h"
#include "tademo/TADemoParser.h"
#include "tademo/GameMonitor.h"

using namespace gpgnet;


QString buildPlayerName(QString baseName, int mean, int deviation, int numgames, QString country)
{
    return baseName;
    //QString playerName = baseName;
    //if (country.size() > 0)
    //{
    //    playerName += QString("-%1").arg(country);
    //}
    //if (numgames > 10)
    //{
    //    playerName += QString("-%1").arg(mean - 2 * deviation);
    //}
    //return playerName;
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
    QStringList oldTaDemos = GetTaDemoFiles(taDemoPaths);
    GameMonitor taDemoMonitor;
    std::shared_ptr<std::istream> taDemoStream;
    QMap<QString, int> gpgPlayerIds;    // remember gpgnet player ids for purpose of settings PlayerOptions

    gpgSend.gameState(gameState = "Idle");
    QString previousState = gameState;
    while (socket.isOpen())
    {
        // look for commands from server
        QVariantList serverCommand;
        QString cmd;

        if (previousState == gameState)
        {
            socket.waitForReadyRead(3000);
        }
        previousState = gameState;
        if (socket.bytesAvailable() > 0)
        {
            serverCommand = GpgNetReceive::GetCommand(ds);
            cmd = serverCommand[0].toString();
            qDebug() << "gpgnet command received:" << cmd;
        }

        if (cmd == "ConnectToPeer")
        {
            ConnectToPeerCommand qtp(serverCommand);
            gpgPlayerIds[qtp.playerName] = qtp.playerId;
            qDebug() << "connect to peer: playername=" << qtp.playerName << "playerId=" << qtp.playerId;
        }

        // look for a new demo file
        if (!taDemoStream && gameState != "Ended")
        {
            QStringList nowTaDemoFiles = GetTaDemoFiles(taDemoPaths);
            qDebug() << "GetTaDemoFiles:" << nowTaDemoFiles.size();
            QStringList newTaDemoFiles = StringListDiff(nowTaDemoFiles, oldTaDemos);
            if (newTaDemoFiles.size() > 0)
            {
                taDemoStream.reset(new std::ifstream(newTaDemoFiles.at(0).toStdString(), std::ios::in | std::ios::binary));
                oldTaDemos = nowTaDemoFiles;

                taDemoMonitor.parse(taDemoStream.get());
                if (taDemoMonitor.isGameOver())
                {
                    // user probably just copied a tad into the directory.  ignore it
                    qDebug() << "new tademo seems a complete game. ignoring" << newTaDemoFiles;
                    taDemoStream.reset();
                    taDemoMonitor.reset();
                }
                else
                {
                    qDebug() << "new tademo found" << newTaDemoFiles;
                }
            }
        }
        else if (taDemoStream)
        {
            taDemoMonitor.parse(taDemoStream.get());
            qDebug() << "taDemoMonitor.parse" << taDemoMonitor.numTimesNewDataReceived();
        }

        if (gameState == "Idle" && !cmd.compare("CreateLobby"))
        {
            createLobbyCommand.Set(serverCommand);
            QString playerName = buildPlayerName(
                createLobbyCommand.playerName, mean, deviation, numGames, country);
            gpgPlayerIds[playerName] = createLobbyCommand.playerId;

            qDebug() << "CreateLobby(playerName:" << playerName << ")";
            jdplay.updatePlayerName(playerName.toStdString().c_str());
            gpgSend.gameState(gameState = "Lobby");
        }
        else if (gameState == "Lobby" && !cmd.compare("HostGame"))
        {
            HostGameCommand hgc(serverCommand);
            QString sessionName = createLobbyCommand.playerName + "'s Game";
            CreateTAInitFile(iniTemplate, iniTarget, sessionName, hgc.mapName, playerLimit, lockOptions);
            qDebug() << "jdplay.initialize(host): guid=" << guid << "mapname=" << hgc.mapName;
            bool ret = jdplay.initialize(guid.toStdString().c_str(), "0.0.0.0", true, 10);
            if (!ret)
            {
                qDebug() << "unable to initialise dplay";
                return;
            }
            qDebug() << "jdplay.launch(host)";
            ret = jdplay.launch(true);
            if (!ret)
            {
                qDebug() << "unable to launch game";
                return;
            }

            gpgSend.gameOption("Slots", 10);
            gameState = "Hosted";
        }
        else if (gameState == "Lobby" && !cmd.compare("JoinGame"))
        {
            JoinGameCommand jgc(serverCommand);
            gpgPlayerIds[jgc.remotePlayerName] = jgc.remotePlayerId;
            qDebug() << "JoinGame guid:" << guid << "remoteHost:" << jgc.remoteHost;

            QStringList candidateHostIps = jgc.remoteHost.split(";");
            for (int nTry = 0; nTry < 3 && gameState != "Joined"; ++nTry)
            {
                Q_FOREACH(QString _hostip, candidateHostIps)
                {
                    char hostip[257] = { 0 };
                    std::strncpy(hostip, _hostip.toStdString().c_str(), 256);

                    qDebug() << "jdplay.initialize(join):" << hostip;
                    bool ret = jdplay.initialize(guid.toStdString().c_str(), hostip, false, 10);
                    if (!ret)
                    {
                        qDebug() << "unable to initialise dplay";
                        return;
                    }

                    if (jdplay.searchOnce())
                    {
                        qDebug() << "jdplay.launch(join):" << hostip;
                        ret = jdplay.launch(true);
                        jdplay.releaseDirectPlay();
                        gameState = "Joined";
                        break;
                    }
                }
            }
            if (gameState != "Joined")
            {
                qDebug() << "unable to find game at any candidate hosts ... quitting";
                return;
            }
        }
        else if (gameState == "Hosted")
        {
            bool active = jdplay.pollStillActive();
            if (active)
            {
                if (taDemoMonitor.isGameStarted())
                {
                    qDebug() << "gamestate Hosted: game started";
                    gpgSend.gameOption("MapName", QString::fromStdString(taDemoMonitor.getMapName()));
                    for (const std::string &playerName: taDemoMonitor.getPlayerNames())
                    {
                        const PlayerData& pd = taDemoMonitor.getPlayerData(playerName);
                        QString playerId = QString::number(gpgPlayerIds.value(QString::fromStdString(playerName)));
                        gpgSend.playerOption(playerId, "Team", 1+pd.teamNumber); // Forged Alliance reserves Team=1 for the team-not-selected team
                        gpgSend.playerOption(playerId, "Army", pd.armyNumber);
                        gpgSend.playerOption(playerId, "StartSpot", pd.teamNumber);
                        gpgSend.playerOption(playerId, "Color", pd.teamNumber);
                        gpgSend.playerOption(playerId, "Faction", static_cast<int>(pd.side));
                    }
                    gpgSend.gameState(gameState = "Launching");
                }
                else
                {
                    qDebug() << "gamestate Hosted: polling dplay lobby";
                    jdplay.pollSessionStatus();
                    jdplay.printSessionDesc();

                    int numPlayers = jdplay.getUserData1() >> 16 & 0x0f;
                    bool closed = jdplay.getUserData1() & 0x80000000;
                    QString mapName = QString::fromStdString(jdplay.getAdvertisedSessionName()).trimmed();
                    qDebug() << "dplay map name:" << mapName;
                    if (mapName.size() > 16)
                    {
                        mapName = mapName.mid(16);
                        gpgSend.gameOption("MapName", mapName);
                    }
                }
            }
            else
            {
                qDebug() << "gamestate Hosted: jdplay not active. terminating";
                jdplay.releaseDirectPlay();
                gpgSend.gameState(gameState = "Ended");
                return;
            }
        }
        else if (gameState == "Joined" || gameState == "Launching") // "Launching" means host hit the start button
        {
            if (taDemoMonitor.isGameOver()) // && taDemoMonitor.numTimesNewDataReceived() > 3u)
            {
                qDebug() << "gameState Joined/Launching: game over";
                // send results
                const GameResult &gameResult = taDemoMonitor.getGameResult();
                for (const auto& result : gameResult.resultByArmy)
                {
                    gpgSend.gameResult(result.first, result.second);
                }

                taDemoStream.reset();
                gameState = "Ended";
                //gpgSend.gameState(gameState = "Ended");
            }
            else
            {
                qDebug() << "gameState Joined/Launching: game in progress";
            }

            if (!jdplay.pollStillActive())
            {
                qDebug() << "gameState Joined/Launching: jdplay not active. terminating";
                // this closes the game on server side and triggers rating update
                jdplay.releaseDirectPlay();
                gpgSend.gameEnded();
                return;
            }
        }
        else if (gameState == "Ended")
        {
            if (!jdplay.pollStillActive())
            {
                // this closes the game on server side and triggers rating update
                qDebug() << "gameState Joined/Launching: jdplay not active. terminating";
                jdplay.releaseDirectPlay();
                gpgSend.gameEnded();
                return;
            }
            else
            {
                qDebug() << "gameState Ended";
            }
        }
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
        while (jdplay.pollStillActive())
        {
            QThread::msleep(1000);
        };
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
