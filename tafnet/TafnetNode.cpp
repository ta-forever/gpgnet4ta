#include "TafnetNode.h"

#include <QtNetwork/qtcpsocket.h>

#ifdef _DEBUG
#include <tademo/HexDump.h>
#endif

using namespace tafnet;

TafnetNode::TafnetNode(std::uint32_t playerId, bool isHost, QHostAddress bindAddress, quint16 bindPort) :
    m_playerId(playerId),
    m_hostPlayerId(isHost ? playerId : 0u)
{
    qDebug() << "[TafnetNode::TafnetNode] playerId" << m_playerId << "udp binding to" << bindAddress.toString() << ":" << bindPort;
    m_lobbySocket.bind(bindAddress, bindPort);
    QObject::connect(&m_lobbySocket, &QUdpSocket::readyRead, this, &TafnetNode::onReadyRead);
}


void TafnetNode::onReadyRead()
{
    QUdpSocket* sender = static_cast<QUdpSocket*>(QObject::sender());

    while (sender->hasPendingDatagrams())
    {
        QByteArray datas;
        datas.resize(sender->pendingDatagramSize());
        QHostAddress senderAddress;
        quint16 senderPort;
        sender->readDatagram(datas.data(), datas.size(), &senderAddress, &senderPort);
        HostAndPort senderHostAndPort(senderAddress, senderPort);
        if (datas.size() < sizeof(TafnetMessageHeader))
        {
            continue;
        }

        auto it = m_peerPlayerIds.find(senderHostAndPort);
        if (it == m_peerPlayerIds.end())
        {
            qDebug() << "[TafnetNode::onReadyRead]" << m_playerId << "ERROR unexpected message from" << senderAddress.toString() << ":" << senderPort;
            continue;
        }
        std::uint32_t peerPlayerId = it->second;

        char* ptr = datas.data();
        const TafnetMessageHeader* tafheader = (TafnetMessageHeader*)ptr;
        ptr += sizeof(TafnetMessageHeader);

        qDebug() << "[TafnetNode::onReadyRead]" << m_playerId << "from" << peerPlayerId << ", action=" << tafheader->action;
        handleMessage(*tafheader, peerPlayerId, ptr, datas.size() - sizeof(TafnetMessageHeader));
    }
}

void TafnetNode::handleMessage(const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len)
{
    m_handleMessage(tafheader, peerPlayerId, data, len);
}

void TafnetNode::setHandler(const std::function<void(const TafnetMessageHeader&, std::uint32_t, char*, int)>& f)
{
    m_handleMessage = f;
}

std::uint32_t TafnetNode::getPlayerId() const
{
    return m_playerId;
}

std::uint32_t TafnetNode::getHostPlayerId() const
{
    return m_hostPlayerId;
}

void TafnetNode::joinGame(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId)
{
    connectToPeer(peer, peerPort, peerPlayerId);
    m_hostPlayerId = peerPlayerId;
}

void TafnetNode::connectToPeer(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId)
{
    qDebug() << "[TafnetNode::connectToPeer]" << m_playerId << "connecting to" << peer.toString() << ":" << peerPort << peerPlayerId;
    HostAndPort hostAndPort(peer, peerPort);
    m_peerAddresses[peerPlayerId] = hostAndPort;
    m_peerPlayerIds[hostAndPort] = peerPlayerId;
    //forwardGameData(peerPlayerId, TafnetMessageHeader::ACTION_HELLO, "", 0);
}

void TafnetNode::forwardGameData(std::uint32_t destPlayerId, std::uint32_t action, const char* data, int len)
{
    auto it = m_peerAddresses.find(destPlayerId);
    if (it == m_peerAddresses.end())
    {
        qDebug() << "[TafnetNode::forwardGameData]" << m_playerId << "ERROR peer" << destPlayerId << "not known";
        return;
    }
    HostAndPort& hostAndPort = it->second;
    qDebug() << "[TafnetNode::forwardGameData]" << m_playerId << "forwarding to" << destPlayerId << ", action=" << action;

#ifdef _DEBUG
    TADemo::HexDump(data, len, std::cout);
#endif

    QByteArray buf;
    buf.resize(sizeof(TafnetMessageHeader) + len);

    TafnetMessageHeader* header = (TafnetMessageHeader*)buf.data();
    header->action = action;
    std::memcpy(buf.data() + sizeof(TafnetMessageHeader), data, len);

    m_lobbySocket.writeDatagram(buf.data(), buf.size(), QHostAddress(hostAndPort.ipv4addr), hostAndPort.port);
    m_lobbySocket.flush();
}
