#include "ConsoleReader.h"
#include <QtCore/qdebug.h>

using namespace taflib;

ConsoleReader::ConsoleReader(QHostAddress addr, quint16 port) :
    m_loggedAConnection(false)
{
    qInfo() << "[ConsoleReader::ConsoleReader] starting server on addr" << addr << "port" << port;
    m_tcpServer.listen(addr, port);
    if (!m_tcpServer.isListening())
    {
        qWarning() << "[ConsoleReader::ConsoleReader] server is not listening!";
    }
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &ConsoleReader::onNewConnection);
}

ConsoleReader::~ConsoleReader()
{
    m_tcpServer.close();
    for (QTcpSocket *s: m_tcpSockets)
    {
      s->close();
    }
}

void ConsoleReader::onNewConnection()
{
    try
    {
        QTcpSocket* socket = m_tcpServer.nextPendingConnection();
        if (!m_loggedAConnection)
        {
            qInfo() << "[ConsoleReader::onNewConnection] accepted connection from" << socket->peerAddress() << "port" << socket->peerPort();
        }
        QObject::connect(socket, &QTcpSocket::readyRead, this, &ConsoleReader::onReadyReadTcp);
        QObject::connect(socket, &QTcpSocket::stateChanged, this, &ConsoleReader::onSocketStateChanged);
        m_tcpSockets.push_back(socket);
    }
    catch (const std::exception & e)
    {
        qWarning() << "[ConsoleReader::onNewConnection] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[ConsoleReader::onNewConnection] general exception:";
    }
}

void ConsoleReader::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            if (!m_loggedAConnection)
            {
                qInfo() << "[ConsoleReader::onSocketStateChanged] peer disconnected" << sender->peerAddress() << "port" << sender->peerPort();
                m_loggedAConnection = true;
            }
            m_tcpSockets.removeOne(sender);
            sender->deleteLater();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[ConsoleReader::onSocketStateChanged] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[ConsoleReader::onSocketStateChanged] general exception:";
    }
}

void ConsoleReader::onReadyReadTcp()
{
    try
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        QByteArray datas = sender->readAll();
        if (datas.size() == 0)
        {
            return;
        }

        QString qline(datas);
        qInfo() << "[ConsoleReader::run] received" << qline;
        emit textReceived(qline);
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
