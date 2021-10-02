#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qdatetime.h>
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

#include "TaReplayServerMessages.h"
#include "TaReplayServer.h"

using namespace tareplay;

static const int MAX_NUM_GAME_OPTIONS = 1000;
static const int CHUNK_SIZE = 1000;

TaReplayServer::UserContext::UserContext(QTcpSocket* socket):
    gameId(0u),
    userDataStream(new QDataStream(socket))
{
    userDataStream->setByteOrder(QDataStream::ByteOrder::LittleEndian);
    userDataStreamProtol.reset(new gpgnet::GpgNetSend(*userDataStream));
    gpgNetParser.reset(new gpgnet::GpgNetParse());
}

TaReplayServer::TaReplayServer(QString demoPathTemplate, QHostAddress addr, quint16 port, quint16 delaySeconds, qint64 maxBytesPerUserPerSecond):
    m_demoPathTemplate(demoPathTemplate),
    m_delaySeconds(delaySeconds),
    m_maxBytesPerUserPerSecond(maxBytesPerUserPerSecond)
{
    qInfo() << "[TaReplayServer::TaReplayServer] starting server on addr" << addr << "port" << port;
    m_tcpServer.listen(addr, port);
    if (!m_tcpServer.isListening())
    {
        qWarning() << "[TaReplayServer::TaReplayServer] server is not listening!";
    }
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &TaReplayServer::onNewConnection);

    startTimer(1000);
}

TaReplayServer::~TaReplayServer()
{
    m_tcpServer.close();
    for (const auto &socket : m_users.keys())
    {
        socket->close();
    }
}

void TaReplayServer::setGameInfo(quint32 gameId, int delaySeconds, QString state)
{
    if (state.compare("ended", Qt::CaseInsensitive) == 0)
    {
        qInfo() << "[TaReplayServer::setGameInfo] removing game" << gameId << "because state ENDED";
        m_gameInfo.remove(gameId);
    }
    else
    {
        qInfo() << "[TaReplayServer::setGameInfo] updating game" << gameId << "info. delay,state=" << delaySeconds << state;
        GameInfo& gameInfo = m_gameInfo[gameId];
        gameInfo.gameId = gameId;
        gameInfo.delaySeconds = delaySeconds;
        gameInfo.state = state;
    }
}

void TaReplayServer::onNewConnection()
{
    try
    {
        while (m_tcpServer.hasPendingConnections())
        {
            QTcpSocket* socket = m_tcpServer.nextPendingConnection();
            socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
            if (m_users.contains(socket))
            {
                qWarning() << "[TaReplayServer::onNewConnection] accepted connection from a socket thats already connected?? pointer" << socket;
            }
            else
            {
                qInfo() << "[TaReplayServer::onNewConnection] accepted connection from" << socket->peerAddress() << "port" << socket->peerPort() << "pointer" << socket;
                QObject::connect(socket, &QTcpSocket::readyRead, this, &TaReplayServer::onReadyRead);
                QObject::connect(socket, &QTcpSocket::stateChanged, this, &TaReplayServer::onSocketStateChanged);
                m_users[socket].reset(new UserContext(socket));
            }
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayServer::onNewConnection] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaReplayServer::onNewConnection] general exception:";
    }
}

void TaReplayServer::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            qInfo() << "[TaReplayServer::onSocketStateChanged] peer disconnected" << sender->peerAddress() << "port" << sender->peerPort() << "pointer" << sender;
            m_users.remove(sender);
            sender->deleteLater();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayServer::onSocketStateChanged] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaReplayServer::onSocketStateChanged] general exception:";
    }
}

void TaReplayServer::onReadyRead()
{
    QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
    try
    {
        auto itUserContext = m_users.find(sender);
        if (itUserContext == m_users.end())
        {
            throw std::runtime_error("received data from unknown socket!");
        }
        if (itUserContext.value().isNull())
        {
            throw std::runtime_error("null user context for socket!");
        }
        if (itUserContext.value()->userDataStream.isNull())
        {
            throw std::runtime_error("null datastream!");
        }
        UserContext& userContext = *itUserContext.value();

        while (!userContext.userDataStream->atEnd())
        {
            QVariantList command = userContext.gpgNetParser->GetCommand(*userContext.userDataStream);
            QString cmd = command[0].toString();

            if (cmd == TaReplayServerSubscribe::ID)
            {
                TaReplayServerSubscribe msg(command);
                if (!m_gameInfo.contains(msg.gameId))
                {
                    qInfo() << "[TaReplayServer::onReadyRead][SUBSCRIBE] GAME NOT FOUND: no entry in m_gameInfo" << msg.gameId;
                    sendData(userContext, TaReplayServerStatus::GAME_NOT_FOUND, QByteArray());
                    continue;
                }
                const GameInfo& game = m_gameInfo[msg.gameId];

                int replayDelaySeconds = game.delaySeconds;
                if (replayDelaySeconds < 0)
                {
                    qInfo() << "[TaReplayServer::onReadyRead][SUBSCRIBE] denying replay since replay is disabled for game" << msg.gameId;
                    sendData(userContext, TaReplayServerStatus::LIVE_REPLAY_DISABLED, QByteArray());
                    continue;
                }

                userContext.demoFile.reset(findReplayFileForGame(msg.gameId));
                if (userContext.demoFile.isNull() || !userContext.demoFile->good())
                {
                    qInfo() << "[TaReplayServer::onReadyRead][SUBSCRIBE] GAME NOT FOUND: bad / not found replay file" << msg.gameId;
                    sendData(userContext, TaReplayServerStatus::GAME_NOT_FOUND, QByteArray());
                    continue;
                }

                if (game.demoFileSizeLog.isNull())
                {
                    qInfo() << "[TaReplayServer::onReadyRead][SUBSCRIBE] GAME NOT FOUND: no size log" << msg.gameId;
                    sendData(userContext, TaReplayServerStatus::GAME_NOT_FOUND, QByteArray());
                    continue;
                }

                if (game.demoFileSizeLog->size() > game.delaySeconds)
                {
                    std::streamoff seekPos = std::min(unsigned(msg.position), unsigned(game.demoFileSizeLog->front()));
                    userContext.demoFile->seekg(seekPos, std::ios::beg);
                }

                 qInfo()
                     << "[TaReplayServer::onReadyRead][SUBSCRIBE] gameId=" << msg.gameId << "position=" << msg.position
                     << "log.size=" << game.demoFileSizeLog->size() << "game.delay=" << game.delaySeconds
                     << "demofile.tellg=" << userContext.demoFile->tellg();
                userContext.gameId = msg.gameId;
            }
            else
            {
                qWarning() << "[TaReplayServer::onReadyRead] unrecognised command" << cmd;
            }
        }
    }
    catch (const gpgnet::GpgNetParse::DataNotReady &)
    { }
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayServer::onReadyRead] exception:" << e.what();
        if (sender)
        {
            qWarning() << "[TaReplayServer::onReadyRead] closing users connection ...";
            sender->close();
        }
    }
    catch (...)
    {
        qWarning() << "[TaReplayServer::onReadyRead] general exception:";
    }
}

std::istream* TaReplayServer::findReplayFileForGame(quint32 gameId)
{
    QFileInfo fileInfo;
    for (QString fn : { m_demoPathTemplate.arg(gameId) + ".part", m_demoPathTemplate.arg(gameId) })
    {
        fileInfo.setFile(fn);
        if (fileInfo.exists() && fileInfo.isFile())
        {
            qInfo() << "[TaReplayServer::findReplayFileForGame] found replay file:" << fn;
            return new std::ifstream(fileInfo.absoluteFilePath().toStdString().c_str(), std::ios::in | std::ios::binary);
        }
    }
    return NULL;
}

void TaReplayServer::sendData(UserContext &user, TaReplayServerStatus status, QByteArray data)
{
    user.userDataStreamProtol->sendCommand(TaReplayServerData::ID, 2);
    user.userDataStreamProtol->sendArgument(int(status));
    user.userDataStreamProtol->sendArgument(data);
}

void TaReplayServer::timerEvent(QTimerEvent* event)
{
    try
    {
        for (auto it = m_gameInfo.begin(); it != m_gameInfo.end(); ++it)
        {
            updateFileSizeLog(it.value());
        }

        for (QSharedPointer<UserContext> userContext : m_users)
        {
            if (userContext && userContext->demoFile)
            {
                serviceUser(*userContext);
            }
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayServer::timerEvent] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaReplayServer::timerEvent] general exception:";
    }
}

void TaReplayServer::updateFileSizeLog(GameInfo& gameInfo)
{
    if (gameInfo.demoFile.isNull())
    {
        gameInfo.demoFile.reset(findReplayFileForGame(gameInfo.gameId));
        if (!gameInfo.demoFile.isNull())
        {
            qInfo() << "[TaReplayServer::updateFileSizeLog] started monitoring replay file for gameId:" << gameInfo.gameId;
            gameInfo.demoFileSizeLog.reset(new QQueue<int>());
        }
    }

    if (!gameInfo.demoFile.isNull())
    {
        gameInfo.demoFile->clear();
        gameInfo.demoFile->seekg(0, std::ios_base::end);
        int size = gameInfo.demoFile->tellg();
        gameInfo.demoFile->clear();
        gameInfo.demoFileSizeLog->enqueue(size);   //back
    }

    if (!gameInfo.demoFileSizeLog.isNull())
    {
        const int requiredLogSize = 1 + gameInfo.delaySeconds;
        while (gameInfo.demoFileSizeLog->size() > requiredLogSize)
        {
            gameInfo.demoFileSizeLog->dequeue();     //front
        }
    }
}

void TaReplayServer::serviceUser(UserContext& user)
{
    QByteArray data;
    int dataEscrowThreshold = 0;

    if (user.demoFile.isNull())
    {
        qWarning() << "[TaReplayServer::serviceUser] user's demoFile for gameid" << user.gameId << "is null!";
        return;
    }
    else if (m_gameInfo.contains(user.gameId) && m_gameInfo[user.gameId].demoFileSizeLog.isNull())
    {
        qWarning() << "[TaReplayServer::serviceUser] filesizelog for gameid" << user.gameId << "is null";
        return;
    }
    else if (m_gameInfo.contains(user.gameId) && m_gameInfo[user.gameId].demoFileSizeLog->isEmpty())
    {
        qWarning() << "[TaReplayServer::serviceUser] filesizelog for gameid" << user.gameId << "is empty";
        return;
    }
    else if (m_gameInfo.contains(user.gameId) && m_gameInfo[user.gameId].demoFileSizeLog->size() > m_gameInfo[user.gameId].delaySeconds)
    {
        dataEscrowThreshold = m_gameInfo[user.gameId].demoFileSizeLog->front();
    }
    else if (!m_gameInfo.contains(user.gameId))
    {
        dataEscrowThreshold = int(user.demoFile->tellg()) + m_maxBytesPerUserPerSecond;
    }
    else
    {
        // including demoFileSizeLog smaller than delaySeconds
        return;
    }

    bool firstBytes = user.demoFile->tellg() == std::streampos(0);
    while(user.userDataStream->device()->bytesToWrite() < m_maxBytesPerUserPerSecond)
    {
        const int maxBytesReveal = dataEscrowThreshold - user.demoFile->tellg();
        const int thisChunkSize = std::min(maxBytesReveal, CHUNK_SIZE);
        if (thisChunkSize <= 0)
        {
            break;
        }

        data.resize(thisChunkSize);
        user.demoFile->clear();
        user.demoFile->read(data.data(), data.size());
        user.demoFile->clear();
        if (user.demoFile->gcount() <= 0)
        {
            break;
        }

        data.truncate(user.demoFile->gcount());
        sendData(user, TaReplayServerStatus::OK, data);
    }

    if (firstBytes && user.userDataStream->device()->bytesToWrite() > 0)
    {
        qInfo() << "[TaReplayServer::serviceUser] sending first chunk of data to user: pointer,gameId,bytes" << QString("%1").arg(quint64(&user), 16, 16) << user.gameId << user.userDataStream->device()->bytesToWrite();
    }
}
