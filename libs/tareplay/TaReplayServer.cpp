#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
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

#include "TaReplayServerMessages.h"
#include "TaReplayServer.h"

using namespace tareplay;

TaReplayServer::UserContext::UserContext(QTcpSocket* socket):
    gameId(0u),
    userDataStream(new QDataStream(socket))
{
    userDataStream->setByteOrder(QDataStream::ByteOrder::LittleEndian);
    userDataStreamProtol.reset(new gpgnet::GpgNetSend(*userDataStream));
    gpgNetParser.reset(new gpgnet::GpgNetParse());
}

TaReplayServer::TaReplayServer(QString demoPathTemplate, QHostAddress addr, quint16 port, quint16 delaySeconds):
    m_demoPathTemplate(demoPathTemplate),
    m_delaySeconds(delaySeconds)
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

void TaReplayServer::onNewConnection()
{
    try
    {
        QTcpSocket* socket = m_tcpServer.nextPendingConnection();
        qInfo() << "[TaReplayServer::onNewConnection] accepted connection from" << socket->peerAddress() << "port" << socket->peerPort();
        QObject::connect(socket, &QTcpSocket::readyRead, this, &TaReplayServer::onReadyRead);
        QObject::connect(socket, &QTcpSocket::stateChanged, this, &TaReplayServer::onSocketStateChanged);
        m_users[socket].reset(new UserContext(socket));
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
            qInfo() << "[TaDemoCompiler::onSocketStateChanged] peer disconnected" << sender->peerAddress() << "port" << sender->peerPort();
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
    try
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        auto itUserContext = m_users.find(sender);
        if (itUserContext == m_users.end())
        {
            qWarning() << "[TaReplayServer::onReadyRead] received data from unknown user!";
            return;
        }
        if (!itUserContext.value()->userDataStream)
        {
            qCritical() << "[TaReplayServer::onReadyRead] user has null datastream!";
            return;
        }
        UserContext& userContext = *itUserContext.value().data();

        while (!itUserContext.value()->userDataStream->atEnd())
        {
            QVariantList command = userContext.gpgNetParser->GetCommand(*userContext.userDataStream);
            QString cmd = command[0].toString();

            if (cmd == TaReplayServerSubscribe::ID)
            {
                TaReplayServerSubscribe msg(command);
                QFileInfo demoFile(m_demoPathTemplate.arg(msg.gameId));
                if (demoFile.exists() && demoFile.isFile())
                {
                    qInfo() << "[TaReplayServer::onReadyRead][SUBSCRIBE] filename=" << demoFile.absoluteFilePath();
                    userContext.demoFile.reset(new std::ifstream(demoFile.absoluteFilePath().toStdString().c_str(), std::ios::in | std::ios::binary));
                    userContext.demoFile->seekg(msg.position);
                    userContext.gameId = msg.gameId;
                }
                else
                {
                    qInfo() << "[TaReplayServer::onReadyRead][SUBSCRIBE] GAME NOT FOUND" << demoFile.absoluteFilePath();
                    sendData(userContext, TaReplayServerStatus::GAME_NOT_FOUND, QByteArray());
                }
            }
            else
            {
                qWarning() << "[TaReplayServer::onReadyRead] unrecognised command" << cmd;
            }
        }
    }
    catch (const gpgnet::GpgNetParse::DataNotReady &)
    {
        qInfo() << "[TaReplayServer::onReadyRead] waiting for more data";
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayServer::onReadyRead] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaReplayServer::onReadyRead] general exception:";
    }
}

void TaReplayServer::sendData(UserContext &user, TaReplayServerStatus status, QByteArray data)
{
    user.userDataStreamProtol->sendCommand(TaReplayServerData::ID, 2);
    user.userDataStreamProtol->sendArgument(int(status));
    user.userDataStreamProtol->sendArgument(data);
}

void TaReplayServer::timerEvent(QTimerEvent* event)
{
    for (QSharedPointer<UserContext> userContext : m_users)
    {
        updateFileSizeLog(*userContext);
        serviceUser(*userContext);
    }
}

void TaReplayServer::updateFileSizeLog(UserContext& user)
{
    if (user.demoFile)
    {
        user.demoFile->clear();
        int pos = user.demoFile->tellg();
        user.demoFile->seekg(0, std::ios_base::end);
        user.demoFileSizeLog.enqueue(user.demoFile->tellg());
        user.demoFile->seekg(pos, std::ios_base::beg);
        user.demoFile->clear();
    }
    else
    {
        user.demoFileSizeLog.enqueue(0);
    }

    while (user.demoFileSizeLog.size() > m_delaySeconds)
    {
        user.demoFileSizeLog.dequeue();
    }
}

void TaReplayServer::serviceUser(UserContext& user)
{
    static const int CHUNK_SIZE = 1000;
    QByteArray data;

    const int dataEscrowThreshold = user.demoFileSizeLog.head();

    for(;;)
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

}
