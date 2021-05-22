#include "GameAddressTranslater.h"
#include "GameReceiver.h"
#include "GameSender.h"
#include "tademo/Watchdog.h"

#include <QtNetwork/qtcpsocket.h>

using namespace tafnet;

GameReceiver::GameReceiver(QHostAddress bindAddress, quint16 tcpPort, quint16 udpPort, QSharedPointer<QUdpSocket> udpSocket) :
    m_bindAddress(bindAddress),
    m_enumPort(0u),
    m_tcpPort(tcpPort),
    m_udpPort(udpPort),
    m_udpSocket(udpSocket)
{
    QObject::connect(&m_enumServer, &QTcpServer::newConnection, this, &GameReceiver::onNewConnection);

    m_tcpServer.listen(bindAddress, tcpPort);
    m_tcpPort = m_tcpServer.serverPort();
    qInfo() << "[GameReceiver::GameReceiver] tcp data server binding to" << m_tcpServer.serverAddress().toString() << ":" << m_tcpServer.serverPort();
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &GameReceiver::onNewConnection);

    m_udpSocket->bind(bindAddress, udpPort);
    m_udpPort = udpSocket->localPort();
    qInfo() << "[GameReceiver::GameReceiver] udp data socket binding" << m_udpSocket->localAddress().toString() << ":" << m_udpSocket->localPort();
    QObject::connect(m_udpSocket.data(), &QTcpSocket::readyRead, this, &GameReceiver::onReadyReadUdp);
}

void GameReceiver::bindEnumerationPort(quint16 port)
{
    qInfo() << "[GameReceiver::bindEnumerationPort] tcp enumeration server binding to" << m_bindAddress << ":" << port;
    if (port > 0)
    {
        m_enumServer.listen(m_bindAddress, port);
        m_enumPort = m_enumServer.serverPort();
        if (m_enumServer.isListening())
        {
            qInfo() << "[GameReceiver::bindEnumerationPort] tcp enumeration server is bound to" << m_enumServer.serverAddress().toString() << ":" << m_enumServer.serverPort();
        }
        else
        {
            qCritical() << "[GameReceiver::bindEnumerationPort] unable to bind!";
        }
    }
    else
    {
        m_enumServer.close();
        m_enumPort = 0;
        qInfo() << "[GameReceiver::bindEnumerationPort] tcp enumeration server is closed";
    }
}

GameReceiver::~GameReceiver()
{
    for (QAbstractSocket *socket : m_sockets)
    {
        socket->disconnectFromHost();
        socket->close();
        delete socket;
    }
}

int GameReceiver::getChannelCodeFromSocket(QAbstractSocket* socket)
{
    if (socket->localPort() == m_enumPort)
    {
        return CHANNEL_ENUM;
    }
    else if (socket->socketType() == QAbstractSocket::SocketType::TcpSocket && socket->localPort() == m_tcpPort)
    {
        return CHANNEL_TCP;
    }
    else if (socket->socketType() == QAbstractSocket::SocketType::UdpSocket && socket->localPort() == m_udpPort)
    {
        return CHANNEL_UDP;
    }
    return 0;
}

void GameReceiver::onNewConnection()
{
    try
    {
        TADemo::Watchdog wd("GameReceiver::onNewConnection", 100);
        QTcpSocket* clientSocket = m_tcpServer.nextPendingConnection();
        if (clientSocket == NULL)
        {
            clientSocket = m_enumServer.nextPendingConnection();
        }
        if (clientSocket == NULL)
        {
            qInfo() << "[GameReceiver::onNewConnection] unexpected connection";
            return;
        }
        qInfo() << "[GameReceiver::onNewConnection]" << clientSocket->localAddress().toString() << ":" << clientSocket->localPort() << "from" << clientSocket->peerAddress().toString();
        QObject::connect(clientSocket, &QTcpSocket::readyRead, this, &GameReceiver::onReadyReadTcp);
        QObject::connect(clientSocket, &QTcpSocket::stateChanged, this, &GameReceiver::onSocketStateChanged);
        m_sockets.push_back(clientSocket);
    }
    catch (std::exception &e)
    {
        qWarning() << "[GameReceiver::onNewConnection] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GameReceiver::onNewConnection] unknown exception";
    }
}

void GameReceiver::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        TADemo::Watchdog wd("GameReceiver::onSocketStateChanged", 100);
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            qInfo() << "[GameReceiver::onSocketStateChanged/UnconnectedState]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString();
            m_sockets.removeOne(sender);
            if (sender->localPort() == m_tcpPort)
            {
                //handleMessage(sender, CHANNEL_TCP, NULL, 0);
            }
            //delete sender;
        }
    }
    catch (std::exception &e)
    {
        qWarning() << "[GameReceiver::onSocketStateChanged] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GameReceiver::onSocketStateChanged] unknown exception";
    }
}

void GameReceiver::onReadyReadTcp()
{
    try
    {
        TADemo::Watchdog wd("GameReceiver::onReadyReadTcp", 100);
        QAbstractSocket* sender = static_cast<QAbstractSocket*>(QObject::sender());
        //qInfo() << "[GameReceiver::onReadyReadTcp]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString() << ":" << sender->peerPort();
        QByteArray datas = sender->readAll();
        handleMessage(sender, getChannelCodeFromSocket(sender), datas.data(), datas.size());
    }
    catch (std::exception &e)
    {
        qWarning() << "[GameReceiver::onReadyReadTcp] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GameReceiver::onReadyReadTcp] unknown exception";
    }
}

void GameReceiver::onReadyReadUdp()
{
    try
    {
        TADemo::Watchdog wd("GameReceiver::onReadyReadUdp", 100);
        QUdpSocket* sender = dynamic_cast<QUdpSocket*>(QObject::sender());
        QByteArray datas;
        datas.resize(sender->pendingDatagramSize());
        QHostAddress senderAddress;
        quint16 senderPort;
        sender->readDatagram(datas.data(), datas.size(), &senderAddress, &senderPort);
        handleMessage(sender, CHANNEL_UDP, datas.data(), datas.size());
    }
    catch (std::exception &e)
    {
        qWarning() << "[GameReceiver::onReadyReadUdp] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GameReceiver::onReadyReadUdp] unknown exception";
    }
}

void GameReceiver::handleMessage(QAbstractSocket* receivingSocket, int channel, char* data, int len)
{
    m_handleMessage(receivingSocket, channel, data, len);
}

void GameReceiver::setHandler(const std::function<void(QAbstractSocket*, int, char*, int)>& f)
{
    m_handleMessage = f;
}

QHostAddress GameReceiver::getBindAddress()
{
    return m_tcpServer.serverAddress();
}

quint16 GameReceiver::getEnumListenPort()
{
    return m_enumPort;
}

quint16 GameReceiver::getTcpListenPort()
{
    return m_tcpPort;
}

quint16 GameReceiver::getUdpListenPort()
{
    return m_udpPort;
}

quint16* GameReceiver::getListenPorts(quint16 ports[2])
{
    ports[0] = m_tcpPort;
    ports[1] = m_udpPort;
    return ports;
}
