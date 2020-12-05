#include "GpgNetRunner.h"
#include "gpgnet/GpgNetSend.h"
#include "gpgnet/GpgNetParse.h"
#include "jdplay/JDPlay.h"

#include <QtNetwork/qtcpsocket.h>
#include <QtCore/qdatastream.h>
#include <QtCore/qsettings.h>
#include <QtCore/qfile.h>

#include <memory>

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

void CreateTAInitFile(QString tmplateFilename, QString iniFilename, QString session, QString mission, int playerLimit, bool lockOptions)
{
    qInfo() << "Loading ta ini template:" << tmplateFilename;
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

    qInfo() << "saving ta ini:" << iniFilename;
    QFile ini(iniFilename);
    if (!ini.open(QFile::WriteOnly | QFile::Text))
    {
        return;
    }

    QTextStream out(&ini);
    ini.write(txt.toUtf8());
}


GpgNetRunner::GpgNetRunner(QString iniTemplate, QString iniTarget, QString gpgneturl,
    double mean, double deviation, QString country, int numGames, QString guid, int playerLimit, bool lockOptions) :
    iniTemplate(iniTemplate),
    iniTarget(iniTarget),
    gpgneturl(gpgneturl),
    mean(mean),
    deviation(deviation),
    country(country),
    numGames(numGames),
    guid(guid),
    playerLimit(playerLimit),
    lockOptions(lockOptions)
{ }

void GpgNetRunner::run()
{
    QTcpSocket socket;
    {
        qInfo() << "Connecting to GPGNet " << gpgneturl;
        QStringList hostAndPort = gpgneturl.split(QRegExp("\\:"));
        socket.connectToHost(hostAndPort[0], hostAndPort[1].toInt());

        if (!socket.waitForConnected(10000))
        {
            qInfo() << "Unable to connect";
            return;
        }
    }
    qInfo() << "Connected";

    QDataStream ds(&socket);
    ds.setByteOrder(QDataStream::LittleEndian);
    GpgNetSend gpgSend(ds);

    QString gameState;
    CreateLobbyCommand createLobbyCommand;
    std::shared_ptr<JDPlay> jdplay(new JDPlay(createLobbyCommand.playerName.toStdString().c_str(), 0, false));

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
            serverCommand = GpgNetParse::GetCommand(ds);
            cmd = serverCommand[0].toString();
            qInfo() << "gpgnet command received:" << cmd;
        }

        if (cmd == "CreateLobby")
        {
            createLobbyCommand.Set(serverCommand);
            gpgPlayerIds[createLobbyCommand.playerName] = createLobbyCommand.playerId;
            emit createLobby(
                createLobbyCommand.protocol, createLobbyCommand.localPort, createLobbyCommand.playerName,
                createLobbyCommand.playerId, createLobbyCommand.natTraversal);
        }
        else if (cmd == "JoinGame")
        {
            JoinGameCommand jgc(serverCommand);
            qInfo() << "join game: playername=" << jgc.remotePlayerName() << "playerId=" << jgc.remotePlayerId;
            gpgPlayerIds[jgc.remotePlayerName()] = jgc.remotePlayerId;
            emit joinGame(jgc.remoteHost(), jgc.remotePlayerName(), jgc.remotePlayerId);
        }
        else if (cmd == "ConnectToPeer")
        {
            ConnectToPeerCommand ctp(serverCommand);
            qInfo() << "connect to peer: playername=" << ctp.playerName() << "playerId=" << ctp.playerId;
            gpgPlayerIds[ctp.playerName()] = ctp.playerId;
            emit connectToPeer(ctp.host(), ctp.playerName(), ctp.playerId);
        }
        else if (cmd == "DisconnectFromPeer")
        {
            DisconnectFromPeerCommand ctp(serverCommand);
            qInfo() << "disconnect from peer: playerid=" << ctp.playerId;
            //gpgPlayerIds erase where value == ctp.playerId; // not super important
            emit disconnectFromPeer(ctp.playerId);
        }

        if (gameState == "Idle" && !cmd.compare("CreateLobby"))
        {
            qInfo() << "CreateLobby(playerName:" << createLobbyCommand.playerName << ")";
            jdplay->updatePlayerName(createLobbyCommand.playerName.toStdString().c_str());
            gpgSend.gameState(gameState = "Lobby");
        }
        else if (gameState == "Lobby" && !cmd.compare("HostGame"))
        {
            HostGameCommand hgc(serverCommand);
            QString sessionName = createLobbyCommand.playerName + "'s Game";
            CreateTAInitFile(iniTemplate, iniTarget, sessionName, hgc.mapName, playerLimit, lockOptions);
            qInfo() << "jdplay.initialize(host): guid=" << guid << "mapname=" << hgc.mapName;
            bool ret = jdplay->initialize(guid.toStdString().c_str(), "127.0.0.1", true, 10);
            if (!ret)
            {
                qInfo() << "unable to initialise dplay";
                return;
            }
            qInfo() << "jdplay.launch(host)";
            ret = jdplay->launch(true);
            if (!ret)
            {
                qInfo() << "unable to launch game";
                return;
            }

            gpgSend.playerOption(QString::number(gpgPlayerIds.value(buildPlayerName(
                createLobbyCommand.playerName, mean, deviation, numGames, country))), "Color", 1);
            gpgSend.gameOption("Slots", 10);
            gameState = "Hosted";
        }
        else if (gameState == "Lobby" && !cmd.compare("JoinGame"))
        {
            JoinGameCommand jgc(serverCommand);
            const char* hostOn47624 = "127.0.0.1"; // game address ... or a GameReceiver proxy
            qInfo() << "JoinGame guid:" << guid;

            for (int nTry = 0; nTry < 3; ++nTry)
            {
                char hostip[257] = { 0 };
                std::strncpy(hostip, hostOn47624, 256);

                qInfo() << "jdplay.initialize(join):" << hostip;
                bool ret = jdplay->initialize(guid.toStdString().c_str(), hostip, false, 10);
                if (!ret)
                {
                    qInfo() << "unable to initialise dplay";
                    return;
                }

                // searching throws a spanner in the works - causes host to reject joiners after launch ...
                if (true)//jdplay->searchOnce())
                {
                    //jdplay->releaseDirectPlay();
                    //jdplay.reset(new JDPlay(createLobbyCommand.playerName.toStdString().c_str(), 0, false));
                    //bool ret = jdplay->initialize(guid.toStdString().c_str(), hostip, false, 10);
                    qInfo() << "jdplay.launch(join):" << hostip;
                    ret = jdplay->launch(true);
                    gpgSend.playerOption(QString::number(gpgPlayerIds.value(buildPlayerName(
                        createLobbyCommand.playerName, mean, deviation, numGames, country))), "Color", 1);
                    gameState = "Joined";
                    break;
                }
            }
            if (gameState != "Joined")
            {
                qInfo() << "unable to find game at any candidate hosts ... quitting";
                jdplay->releaseDirectPlay();
                return;
            }
        }
        else if (gameState == "Hosted")
        {
            bool active = jdplay->pollStillActive();
            if (active)
            {
                if (gamestarted)
                {
                    //qInfo() << "gamestate Hosted: game started";
                    gpgSend.gameOption("MapName", mapName);
                    for (const std::string& playerName : playernames)
                    {
                        const PlayerData& pd = playerdata;
                        QString playerId = QString::number(gpgPlayerIds.value(QString::fromStdString(playerName)));
                        gpgSend.playerOption(playerId, "Team", 1 + pd.teamNumber); // Forged Alliance reserves Team=1 for the team-not-selected team
                        gpgSend.playerOption(playerId, "Army", pd.armyNumber);
                        gpgSend.playerOption(playerId, "StartSpot", pd.teamNumber);
                        gpgSend.playerOption(playerId, "Color", pd.teamNumber);
                        gpgSend.playerOption(playerId, "Faction", static_cast<int>(pd.side));
                    }
                    gpgSend.gameState(gameState = "Launching");
                }
                else
                {
                    if (mapName.size() > 16)
                    {
                        mapName = mapName.mid(16);
                        gpgSend.gameOption("MapName", mapName);
                    }
                }
            }
            else
            {
                qInfo() << "gamestate Hosted: jdplay not active. terminating";
                jdplay->releaseDirectPlay();
                gpgSend.gameState(gameState = "Ended");
                return;
            }
        }
        else if (gameState == "Joined" || gameState == "Launching") // "Launching" means host hit the start button
        {
            if (isgameover)
            {
                qInfo() << "gameState Joined/Launching: game over";
                // send results
                const GameResult& gameResult = gameresult;
                for (const auto& result : gameResult.resultByArmy)
                {
                    gpgSend.gameResult(result.first, result.second);
                }

                gameState = "Ended";
                //gpgSend.gameState(gameState = "Ended");
            }
            else
            {
                qDebug() << "gameState Joined/Launching: game in progress";
            }

            if (!jdplay->pollStillActive())
            {
                qInfo() << "gameState Joined/Launching: jdplay not active. terminating";
                // this closes the game on server side and triggers rating update
                jdplay->releaseDirectPlay();
                gpgSend.gameEnded();
                return;
            }
        }
        else if (gameState == "Ended")
        {
            if (!jdplay->pollStillActive())
            {
                // this closes the game on server side and triggers rating update
                qInfo() << "gameState Joined/Launching: jdplay not active. terminating";
                jdplay->releaseDirectPlay();
                gpgSend.gameEnded();
                return;
            }
            else
            {
                qInfo() << "gameState Ended";
            }
        }
    }
}