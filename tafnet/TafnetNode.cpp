#include "TafnetNode.h"

#include <QtNetwork/qtcpsocket.h>

#ifdef _DEBUG
#include <tademo/HexDump.h>
#endif

using namespace tafnet;


void TafnetNode::onNewConnection()
{
    QTcpSocket* clientSocket = m_tcpServer.nextPendingConnection();
    qDebug() << "[TafnetNode::onNewConnection]" << m_tafnetId << "from" << clientSocket->peerAddress().toString();
    QObject::connect(clientSocket, &QTcpSocket::readyRead, this, &TafnetNode::onReadyRead);
    QObject::connect(clientSocket, &QTcpSocket::stateChanged, this, &TafnetNode::onSocketStateChanged);
    m_remoteTafnetIds[clientSocket] = 0u;
}

void TafnetNode::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    if (socketState == QAbstractSocket::UnconnectedState)
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        std::uint32_t remoteTafnetId = m_remoteTafnetIds[sender];
        qDebug() << "[TafnetNode::onSocketStateChanged/disconnect]" << m_tafnetId << "from" << sender->peerAddress().toString() << remoteTafnetId;
        m_remoteTafnetIds.remove(sender);
        m_tcpSockets.remove(remoteTafnetId);
        delete sender;
    }
}

void TafnetNode::onReadyRead()
{
    QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
    QByteArray datas = sender->readAll();

    char* ptr = datas.data();
    int remain = datas.size();
    while (ptr < datas.data() + datas.size())
    {
        const TafnetMessageHeader* tafheader = (TafnetMessageHeader*)ptr;
        ptr += sizeof(TafnetMessageHeader);
        remain -= sizeof(TafnetMessageHeader);

        qDebug() << "[TafnetNode::onReadyRead]" << m_tafnetId << "from" << tafheader->sourceId << ", action=" << tafheader->action;

        if (m_remoteTafnetIds[sender] == 0u &&
            tafheader->action == TafnetMessageHeader::ACTION_HELLO)
        {
            m_remoteTafnetIds[sender] = tafheader->sourceId;
            m_tcpSockets[tafheader->sourceId] = sender;
            if (tafheader->destId == 0)
            {
                // source doesn't know who we are.  reply the hello
                sendHello(sender);
            }
        }

        if (remain >= (int)tafheader->data_bytes)
        {
            handleMessage(*tafheader, ptr, tafheader->data_bytes);
        }
        ptr += tafheader->data_bytes;
        remain -= tafheader->data_bytes;
    }
}

void TafnetNode::sendHello(QTcpSocket* socket)
{
    TafnetMessageHeader header;
    header.action = TafnetMessageHeader::ACTION_HELLO;
    header.sourceId = m_tafnetId;
    header.destId = m_remoteTafnetIds[socket];  // 0 if unknown
    header.data_bytes = 0;
    socket->write((const char*)&header, sizeof(header));
    socket->flush();
}

void TafnetNode::handleMessage(const TafnetMessageHeader& tafheader, char* data, int len)
{
    m_handleMessage(tafheader, data, len);
}

TafnetNode::TafnetNode(std::uint32_t tafnetId, QHostAddress bindAddress, quint16 bindPort) :
    m_tafnetId(tafnetId)
{
    qDebug() << "[TafnetNode::TafnetNode] node" << m_tafnetId << "tcp binding to" << bindAddress.toString() << ":" << bindPort;
    m_tcpServer.listen(bindAddress, bindPort);
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &TafnetNode::onNewConnection);
}

void TafnetNode::setHandler(const std::function<void(const TafnetMessageHeader&, char*, int)>& f)
{
    m_handleMessage = f;
}

std::uint32_t TafnetNode::getTafnetId()
{
    return m_tafnetId;
}

bool TafnetNode::connectToPeer(QHostAddress peer, quint16 peerPort)
{
    qDebug() << "[TafnetNode::connectToPeer] node" << m_tafnetId << "connecting to" << peer.toString() << ":" << peerPort;
    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(peer, peerPort);
    socket->waitForConnected(2000);
    if (socket->isOpen())
    {
        QObject::connect(socket, &QTcpSocket::readyRead, this, &TafnetNode::onReadyRead);
        QObject::connect(socket, &QTcpSocket::stateChanged, this, &TafnetNode::onSocketStateChanged);
        m_remoteTafnetIds[socket] = 0;
        sendHello(socket);
        return true;
    }
    else
    {
        delete socket;
        return false;
    }
}

void TafnetNode::forwardGameData(std::uint32_t destNodeId, std::uint32_t action, char* data, int len)
{
    qDebug() << "[TafnetNode::forwardGameData] node" << m_tafnetId << "forwarding to node" << destNodeId << ", action=" << action;
    if (m_tcpSockets.count(destNodeId) == 0)
    {
        return;
    }

#ifdef _DEBUG
    TADemo::HexDump(data, len, std::cout);
#endif

    QByteArray buf;
    buf.resize(sizeof(TafnetMessageHeader) + len);

    TafnetMessageHeader* header = (TafnetMessageHeader*)buf.data();
    header->sourceId = this->getTafnetId();
    header->destId = destNodeId;
    header->action = action;
    header->data_bytes = len;
    std::memcpy(buf.data() + sizeof(header), data, len);

    QTcpSocket* socket = m_tcpSockets[destNodeId];
    socket->write(buf);
    socket->flush();
}
