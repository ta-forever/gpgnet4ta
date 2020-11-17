#include "GameAddressTranslater.h"
#include "GameReceiver.h"
#include "GameSender.h"

#include <QtNetwork/qtcpsocket.h>

using namespace tafnet;

GameReceiver::GameReceiver(QHostAddress bindAddress, quint16 enumPort, quint16 tcpPort, quint16 udpPort, GameSender* sender) :
    m_bindAddress(bindAddress),
    m_enumPort(enumPort),
    m_tcpPort(tcpPort),
    m_udpPort(udpPort),
    m_sender(sender),
    m_udpSocket(sender->getUdpSocket())
{
    m_enumServer.listen(bindAddress, enumPort);
    m_enumPort = m_enumServer.serverPort();
    qDebug() << "[GameReceiver::GameReceiver] tcp enumeration server binding to" << m_enumServer.serverAddress().toString() << ":" << m_enumPort;
    QObject::connect(&m_enumServer, &QTcpServer::newConnection, this, &GameReceiver::onNewConnection);

    m_tcpServer.listen(bindAddress, tcpPort);
    m_tcpPort = m_tcpServer.serverPort();
    qDebug() << "[GameReceiver::GameReceiver] tcp data server binding to" << m_tcpServer.serverAddress().toString() << ":" << m_tcpPort;
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &GameReceiver::onNewConnection);

    QUdpSocket* udpSocket = m_udpSocket.data(); //new QUdpSocket();
    udpSocket->bind(bindAddress, udpPort);
    m_udpPort = udpSocket->localPort();
    qDebug() << "[GameReceiver::GameReceiver] udp data socket binding" << udpSocket->localAddress().toString() << ":" << m_udpPort;
    QObject::connect(udpSocket, &QTcpSocket::readyRead, this, &GameReceiver::onReadyReadUdp);
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
    QTcpSocket* clientSocket = m_tcpServer.nextPendingConnection();
    if (clientSocket == NULL)
    {
        clientSocket = m_enumServer.nextPendingConnection();
    }
    if (clientSocket == NULL)
    {
        qDebug() << "[GameReceiver::onNewConnection] unexpected connection";
        return;
    }
    qDebug() << "[GameReceiver::onNewConnection]" << clientSocket->localAddress().toString() << ":" << clientSocket->localPort() << "from" << clientSocket->peerAddress().toString();
    QObject::connect(clientSocket, &QTcpSocket::readyRead, this, &GameReceiver::onReadyReadTcp);
    QObject::connect(clientSocket, &QTcpSocket::stateChanged, this, &GameReceiver::onSocketStateChanged);
    m_sockets.push_back(clientSocket);
}

void GameReceiver::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    if (socketState == QAbstractSocket::UnconnectedState)
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        qDebug() << "[GameReceiver::onSocketStateChanged/UnconnectedState]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString();
        m_sockets.removeOne(sender);
        //delete sender;
    }
}

void GameReceiver::setSenderGamePorts(char* data, int len)
{
    DPHeader* header = (DPHeader*)data;
    if (m_sender && len >= sizeof(DPHeader) && header->looksOk())
    {
        m_sender->setTcpPort(header->address.port());
        m_sender->setUdpPort(header->address.port() + 50);
        //m_sender = NULL;
    }
}

void GameReceiver::onReadyReadTcp()
{
    QAbstractSocket* sender = static_cast<QAbstractSocket*>(QObject::sender());
    qDebug() << "[GameReceiver::onReadyReadTcp]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString() << ":" << sender->peerPort();
    QByteArray datas = sender->readAll();
    setSenderGamePorts(datas.data(), datas.size());
    handleMessage(sender, getChannelCodeFromSocket(sender), datas.data(), datas.size());
}

void GameReceiver::onReadyReadUdp()
{
    //QByteArray datas;
    //datas.resize(m_proxySocket.pendingDatagramSize());
    //QHostAddress sender;
    //quint16 senderPort;
    //m_proxySocket.readDatagram(datas.data(), datas.size(), &sender, &senderPort);
    QUdpSocket* sender = dynamic_cast<QUdpSocket*>(QObject::sender());
    qDebug() << "[GameReceiver::onReadyReadUdp]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString() << ":" << sender->peerPort();
    QByteArray datas;
    datas.resize(sender->pendingDatagramSize());
    QHostAddress senderAddress;
    quint16 senderPort;
    sender->readDatagram(datas.data(), datas.size(), &senderAddress, &senderPort);
    setSenderGamePorts(datas.data(), datas.size());
    handleMessage(sender, CHANNEL_UDP, datas.data(), datas.size());
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
