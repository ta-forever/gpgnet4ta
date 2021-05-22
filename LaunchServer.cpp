#include "LaunchServer.h"

#include "tademo/HexDump.h"

#include <sstream>
#include <QtCore/qregularexpression.h>
#include <QtCore/qsettings.h>
#include <QtCore/qthread.h>

static const int KEEPALIVE_TIMEOUT = 10;

LaunchServer::LaunchServer(QHostAddress addr, quint16 port):
    m_shutdownCounter(KEEPALIVE_TIMEOUT),
    m_loggedAConnection(false)
{
    qInfo() << "[LaunchServer::LaunchServer] starting server on addr" << addr << "port" << port;
    m_tcpServer.listen(addr, port);
    if (!m_tcpServer.isListening())
    {
        qWarning() << "[LaunchServer::LaunchServer] launch server is not listening!";
    }
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &LaunchServer::onNewConnection);
    QObject::startTimer(1000);
}

void LaunchServer::onNewConnection()
{
    try
    {
        QTcpSocket* socket = m_tcpServer.nextPendingConnection();
        if (!m_loggedAConnection)
        {
            qInfo() << "accepted connection from" << socket->peerAddress() << "port" << socket->peerPort();
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
            m_shutdownCounter = KEEPALIVE_TIMEOUT;
            return;
        }
        else if (args.size() >= 4 && args[0] == "/host")
        {
            launchGame(args[1], args[2], args[3], true);
        }
        else if (args.size() >= 4 && args[0] == "/join")
        {
            launchGame(args[1], args[2], args[3], false);
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

void LaunchServer::launchGame(QString _guid, QString _player, QString _ipaddr, bool asHost)
{
    if (m_jdPlay)
    {
        return;
    }

    if (asHost)
    {
        qInfo() << "host" << _guid << _player << _ipaddr;
    }
    else
    {
        qInfo() << "join" << _guid << _player << _ipaddr;
    }

    std::string guid = _guid.toStdString();
    std::string player = _player.toStdString();
    std::string ipaddr = _ipaddr.toStdString();

    m_jdPlay.reset(new JDPlay(player.c_str(), 3, false));
    if (!m_jdPlay->initialize(guid.c_str(), ipaddr.c_str(), asHost, 10))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to initialise!";
        m_jdPlay.reset();
        notifyClients("FAIL");
        emit gameFailedToLaunch(_guid);
        return;
    }
    if (!m_jdPlay->launch(true))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to launch!";
        m_jdPlay.reset();
        notifyClients("FAIL");
        emit gameFailedToLaunch(_guid);
        return;
    }
    notifyClients("RUNNING");
}

void LaunchServer::timerEvent(QTimerEvent* event)
{
    try
    {
        if (m_jdPlay && !m_jdPlay->pollStillActive())
        {
            qInfo() << "[LaunchServer::timerEvent] jdlay not active. informing clients";
            notifyClients("IDLE");
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
