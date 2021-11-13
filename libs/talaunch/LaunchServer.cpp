#include "LaunchServer.h"

#include "taflib/HexDump.h"

#include <sstream>
#include <QtCore/qregularexpression.h>
#include <QtCore/qsettings.h>
#include <QtCore/qthread.h>

using namespace talaunch;

static const int TICK_RATE_MILLISEC = 1000;

LaunchServer::LaunchServer(QHostAddress addr, quint16 port, int keepAliveTimeout):
    m_keepAliveTimeout(keepAliveTimeout * 1000 / TICK_RATE_MILLISEC),
    m_shutdownCounter(keepAliveTimeout),
    m_loggedAConnection(false)
{
    qInfo() << "[LaunchServer::LaunchServer] starting server on addr" << addr << "port" << port;
    m_tcpServer.listen(addr, port);
    if (!m_tcpServer.isListening())
    {
        qWarning() << "[LaunchServer::LaunchServer] launch server is not listening!";
    }
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &LaunchServer::onNewConnection);
    QObject::startTimer(TICK_RATE_MILLISEC);
}

void LaunchServer::onNewConnection()
{
    try
    {
        QTcpSocket* socket = m_tcpServer.nextPendingConnection();
        if (!m_loggedAConnection)
        {
            qInfo() << "[LaunchServer::onNewConnection] accepted connection from" << socket->peerAddress() << "port" << socket->peerPort();
        }
        QObject::connect(socket, &QTcpSocket::readyRead, this, &LaunchServer::onReadyReadTcp);
        QObject::connect(socket, &QTcpSocket::stateChanged, this, &LaunchServer::onSocketStateChanged);
        m_tcpSockets.push_back(socket);
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::onNewConnection] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::onNewConnection] general exception:";
    }
}

void LaunchServer::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            if (!m_loggedAConnection)
            {
                qInfo() << "[LaunchServer::onSocketStateChanged] peer disconnected" << sender->peerAddress() << "port" << sender->peerPort();
                m_loggedAConnection = true;
            }
            m_tcpSockets.removeOne(sender);
            sender->deleteLater();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::onSocketStateChanged] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::onSocketStateChanged] general exception:";
    }
}

void LaunchServer::onReadyReadTcp()
{
    try
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        QByteArray datas = sender->readAll();
        QStringList args = QString::fromUtf8(datas.data(), datas.size()).replace(QRegularExpression("[\n\r]"), "").split(' ');

        if (args.size() == 0)
        {
            return;
        }
        else if (args[0] == "/keepalive")
        {
            if (!m_loggedAConnection)
            {
                qInfo() << "[LaunchServer::onReadyReadTcp] received keepalive message" << sender->peerAddress() << "port" << sender->peerPort();
            }
            m_shutdownCounter = m_keepAliveTimeout;
            return;
        }
        else if (args.size() >= 4 && args[0] == "/host")
        {
            launchGame(args[1], args[2], args[3], true, false);
        }
        else if (args.size() >= 4 && args[0] == "/join")
        {
            launchGame(args[1], args[2], args[3], false, false);
        }
        else if (args.size() >= 4 && args[0] == "/searchjoin")
        {
            launchGame(args[1], args[2], args[3], false, true);
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::onReadyReadTcp] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::onReadyReadTcp] general exception:";
    }
}

void LaunchServer::launchGame(QString _guid, QString _player, QString _ipaddr, bool asHost, bool doSearch)
{
    if (m_jdPlay)
    {
        return;
    }

    std::string guid = _guid.toStdString();
    std::string player = _player.toStdString();
    std::string ipaddr = _ipaddr.toStdString();

    if (asHost)
    {
        qInfo() << "host" << guid.c_str() << player.c_str() << ipaddr.c_str();
    }
    else if (doSearch)
    {
        qInfo() << "searchjoin" << guid.c_str() << player.c_str() << ipaddr.c_str();
    }
    else
    {
        qInfo() << "join" << guid.c_str() << player.c_str() << ipaddr.c_str();
    }

    m_jdPlay.reset(new jdplay::JDPlay(player.c_str(), 1, false));
    if (!m_jdPlay->initialize(guid.c_str(), ipaddr.c_str(), asHost, 10))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to initialise!" << m_jdPlay->getLastError().c_str();
        qInfo() << "[LaunchServer::launchGame] enumSessionLog:\n" << m_jdPlay->getEnumSessionLog().c_str();
        m_jdPlay.reset();
        notifyClients("FAIL");
        emit gameFailedToLaunch(_guid);
        return;
    }
    else if (!asHost && doSearch && !(m_jdPlay->searchOnce() || m_jdPlay->searchOnce() || m_jdPlay->searchOnce()))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to find a game!" << m_jdPlay->getLastError().c_str();
        qInfo() << "[LaunchServer::launchGame] enumSessionLog:\n" << m_jdPlay->getEnumSessionLog().c_str();
        m_jdPlay.reset();
        notifyClients("FAIL");
        emit gameFailedToLaunch(_guid);
        return;
    }
    else if (!m_jdPlay->launch(true))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to launch!" << m_jdPlay->getLastError().c_str();
        qInfo() << "[LaunchServer::launchGame] enumSessionLog:\n" << m_jdPlay->getEnumSessionLog().c_str();
        m_jdPlay.reset();
        notifyClients("FAIL");
        emit gameFailedToLaunch(_guid);
        return;
    }
    else {
        qInfo() << "[LaunchServer::launchGame] success";
        qInfo() << "[LaunchServer::launchGame] enumSessionLog:\n" << m_jdPlay->getEnumSessionLog().c_str();
        notifyClients("RUNNING");
    }
}

void LaunchServer::timerEvent(QTimerEvent* event)
{
    try
    {
        DWORD exitCode;
        if (m_jdPlay && !m_jdPlay->pollStillActive(exitCode))
        {
            qInfo() << QString("[LaunchServer::timerEvent] process exited with code 0x%1 (%2)").arg(exitCode, 8, 16, QChar('0')).arg(qint32(exitCode), 0, 10);
            if (exitCode == 0)
            {
                notifyClients("IDLE");
            }
            else
            {
                notifyClients(QString("FAIL %1").arg(exitCode, 0, 16, QChar('0')));
                emit gameExitedWithError(exitCode);
            }
            m_jdPlay.reset();
        }

        --m_shutdownCounter;
        if (m_shutdownCounter <= 0)
        {
            qInfo() << "[LaunchServer::timerEvent] shutdown counter expired.  terminating";
            emit quit();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::timerEvent] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::timerEvent] general exception:";
    }
}

void LaunchServer::notifyClients(QString _msg)
{
    std::string msg = _msg.toStdString();
    for (QTcpSocket* socket : m_tcpSockets)
    {
        socket->write(msg.c_str(), 1+msg.size());
        socket->flush();
    }
}
