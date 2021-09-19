#include "TafLobbyClient.h"
#include "TafLobbyJsonProtocol.h"

#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qcryptographichash.h>


TafLobbyPlayerInfo::TafLobbyPlayerInfo() :
    id(0),
    login("<login>")
{ }

TafLobbyPlayerInfo::TafLobbyPlayerInfo(const QJsonObject& playerInfo) :
    id(playerInfo.value("id").toInt()),
    login(playerInfo.value("login").toString())
{ }

TafLobbyGameInfo::TafLobbyGameInfo():
    id(0),
    host("<host>"),
    title("<title>"),
    featuredMod("<featuredMod>"),
    mapName("<mapName>"),
    mapFilePath("<mapFilePath>"),
    gameType("<gameType>"),
    ratingType("<ratingType>"),
    visibility("public"),
    passwordProtected(false),
    state("<state>"),
    replayDelaySeconds(300),
    numPlayers(0),
    maxPlayers(10)
{ }

TafLobbyGameInfo::TafLobbyGameInfo(const QJsonObject& gameInfo):
    id(gameInfo.value("uid").toInt()),
    host(gameInfo.value("host").toString()),
    title(gameInfo.value("title").toString()),
    featuredMod(gameInfo.value("featured_mod").toString()),
    mapName(gameInfo.value("map_name").toString()),
    mapFilePath(gameInfo.value("map_file_path").toString()),
    gameType(gameInfo.value("game_type").toString()),
    ratingType(gameInfo.value("rating_type").toString()),
    visibility(gameInfo.value("visibility").toString()),
    passwordProtected(gameInfo.value("password_protected").toBool()),
    state(gameInfo.value("state").toString()),
    replayDelaySeconds(gameInfo.value("replay_delay_seconds").toInt(300)),
    numPlayers(gameInfo.value("num_players").toInt()),
    maxPlayers(gameInfo.value("max_players").toInt())
{
    const QJsonObject _teams = gameInfo.value("teams").toObject();
    for (auto it = _teams.begin(); it != _teams.end(); ++it)
    {
        QJsonArray teamMembersJsonArray = it.value().toArray();
        QStringList teamMembersStringList;
        std::transform(teamMembersJsonArray.begin(), teamMembersJsonArray.end(), std::back_inserter(teamMembersStringList), [](const QJsonValue& memberJson)
        {
            return memberJson.toString();
        });
        teams[it.key()] = teamMembersStringList;
    }
}

TafLobbyClient::TafLobbyClient(QString userAgentName, QString userAgentVersion) :
    m_userAgentName(userAgentName),
    m_userAgentVersion(userAgentVersion),
    m_protocol(&m_socket)
{
    QObject::connect(&m_socket, &QTcpSocket::readyRead, this, &TafLobbyClient::onReadyRead);
    QObject::connect(&m_socket, &QTcpSocket::stateChanged, this, &TafLobbyClient::onSocketStateChanged);
    startTimer(1000);
}

TafLobbyClient::~TafLobbyClient()
{
}

void TafLobbyClient::connectToHost(QString hostName, quint16 port)
{
    qInfo() << "[TafLobbyClient::connectToHost]" << hostName, port;
    m_hostName = hostName;
    m_port = port;
}

void TafLobbyClient::sendAskSession()
{
    qInfo() << "[TafLobbyClient::sendAskSession]";
    QJsonObject cmd;
    cmd.insert("command", "ask_session");
    cmd.insert("user_agent", m_userAgentName);
    cmd.insert("version", m_userAgentVersion);
    m_protocol.sendJson(cmd);
}

void TafLobbyClient::sendHello(qint64 session, QString uniqueId, QString localIp, QString login, QString password)
{
    qInfo() << "[TafLobbyClient::sendHello]" << session << uniqueId << localIp << login << "****";
    QJsonObject cmd;
    cmd.insert("command", "hello");
    cmd.insert("session", QJsonValue(session));
    cmd.insert("unique_id", uniqueId);
    cmd.insert("local_ip", localIp);
    cmd.insert("login", login);
    cmd.insert("password", password);
    m_protocol.sendJson(cmd);
}

void TafLobbyClient::sendPong()
{
    QJsonObject cmd;
    cmd.insert("command", "pong");
    m_protocol.sendJson(cmd);
}

void TafLobbyClient::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            qWarning() << "[TafLobbyClient::onSocketStateChanged] socket disconnected";
        }
        else if (socketState == QAbstractSocket::ConnectedState)
        {
            qInfo() << "[TafLobbyClient::onSocketStateChanged] socket connected";
            sendAskSession();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TafLobbyClient::onSocketStateChanged] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TafLobbyClient::onSocketStateChanged] general exception:";
    }
}


void TafLobbyClient::timerEvent(QTimerEvent* event)
{
    try
    {
        if (m_socket.state() == QAbstractSocket::UnconnectedState && !m_hostName.isEmpty() && m_port > 0)
        {
            m_socket.connectToHost(m_hostName, m_port);
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TafLobbyClient::timerEvent] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TafLobbyClient::timerEvent] general exception:";
    }
}

void TafLobbyClient::onReadyRead()
{
    try
    {
        while (m_socket.bytesAvailable() > 0)
        {
            QJsonObject cmd;
            m_protocol.receiveJson(cmd);
            auto it = cmd.find("command");
            if (it.value() == "notice")
            {
                emit notice(cmd.value("style").toString(), cmd.value("text").toString());
            }
            else if (it.value() == "session")
            {
                emit session(cmd.value("session").toInt());
            }
            else if (it.value() == "welcome")
            {
                emit welcome(TafLobbyPlayerInfo(cmd.value("me").toObject()));
            }
            else if (it.value() == "player_info")
            {
                for (const QJsonValue& player : cmd.value("players").toArray())
                {
                    emit playerInfo(TafLobbyPlayerInfo(player.toObject()));
                }
            }
            else if (it.value() == "social")
            {
            }
            else if (it.value() == "game_info")
            {
                if (cmd.contains("games"))
                {
                    for (const QJsonValue& game : cmd.value("games").toArray())
                    {
                        emit gameInfo(TafLobbyGameInfo(game.toObject()));
                    }
                }
                else
                {
                    emit gameInfo(TafLobbyGameInfo(cmd));
                }
            }
            else if (it.value() == "ping")
            {
                sendPong();
            }
            else
            {
                qDebug() << "[TafLobbyClient::onReadyRead] unknown command:";
                for (auto it = cmd.begin(); it != cmd.end(); ++it)
                {
                    qDebug() << it.key() << ':' << it.value();
                }
            }
        }
    }
    catch (const TafLobbyJsonProtocol::DataNotReady&)
    {
        qInfo() << "[TafLobbyClient::onReadyRead] wait more data";
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TafLobbyClient::onReadyRead] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TafLobbyClient::onReadyRead] general exception:";
    }
}
