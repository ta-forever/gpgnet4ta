#include "TafnetNode.h"

#include <QtNetwork/qtcpsocket.h>

#ifdef _DEBUG
#include <tademo/HexDump.h>
#endif

//#define SIM_PACKET_LOSS 50
#ifdef SIM_PACKET_LOSS
#include <random>
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(0, 99);
#endif

using namespace tafnet;

Payload::Payload():
action(ACTION_INVALID)
{ }

void Payload::set(std::uint8_t _action, const char *data, int len)
{
    if (data == NULL)
    {
        qWarning() << "[Payload::set] attempt to set null payload!";
    }
    if (len == 0)
    {
        qWarning() << "[Payload::set] attempt to set zero sized payload!";
    }
    action = _action;
    buf.reset(new QByteArray(data, len));
    if (!buf)
    {
        qWarning() << "[Payload::set] initialised payload is NULL!";
    }
}

DataBuffer::DataBuffer() :
m_nextPopSeq(1u),
m_nextPushSeq(1u)
{ }

void DataBuffer::reset()
{
    m_data.clear();
    m_nextPopSeq = 1u;
    m_nextPushSeq = 1u;
}

void DataBuffer::insert(std::uint32_t seq, std::uint8_t action, const char *data, int len)
{
    if (seq >= m_nextPopSeq)
    {
        m_data[seq].set(action, data, len);
    }
}

std::uint32_t DataBuffer::push_back(std::uint8_t action, const char *data, int len)
{
    m_data[m_nextPushSeq].set(action, data, len);
    return m_nextPushSeq++;
}

Payload DataBuffer::pop()
{
    Payload result;
    if (readyRead())
    {
        result = m_data.begin()->second;
        m_data.erase(m_data.begin());
        ++m_nextPopSeq;
    }
    return result;
}

std::size_t DataBuffer::size()
{
    return m_data.size();
}


bool DataBuffer::ackData(std::uint32_t seq)
{
    return m_data.erase(seq) > 0u;
}

bool DataBuffer::readyRead()
{
    return !m_data.empty() && m_nextPopSeq == m_data.begin()->first;
}

bool DataBuffer::empty()
{
    return m_data.empty();
}

std::uint32_t DataBuffer::nextExpectedPopSeq()
{
    return m_nextPopSeq;
}


std::uint32_t DataBuffer::earliestAvailable()
{
    return m_data.begin()->first;
}

Payload DataBuffer::get(std::uint32_t seq)
{
    Payload result;
    auto it = m_data.find(seq);
    if (it != m_data.end())
    {
        result = it->second;
    }
    return result;
}

std::map<std::uint32_t, Payload > & DataBuffer::getAll()
{
    return m_data;
}

int TafnetNode::ResendRate::get(bool incSendCount)
{
    if (ackCount > 32)
    {
        sendCount /= 2;
        ackCount /= 2;
    }

    int rate;
    if (sendCount <= ackCount)
    {
        rate = 1;
    }
    else if (ackCount == 0)
    {
        rate = sendCount;
    }
    else
    {
        rate = sendCount / ackCount;
    }

    static const int MAXRATE = 3;
    if (rate > MAXRATE)
    {
        rate = MAXRATE;
        if (ackCount > 0u)
        {
            sendCount = ackCount * (MAXRATE + 1) - 1;
        }
    }

    if (incSendCount)
    {
        sendCount += std::max(1,rate-1);
    }

    return rate;
}

TafnetNode::HostAndPort::HostAndPort() :
ipv4addr(0),
port(0)
{ }

TafnetNode::HostAndPort::HostAndPort(QHostAddress addr, std::uint16_t port) :
ipv4addr(addr.toIPv4Address()),
port(port)
{ }

bool TafnetNode::HostAndPort::operator< (const HostAndPort& other) const
{
    return (port < other.port) || (port == other.port) && (ipv4addr < other.ipv4addr);
}

TafnetNode::TafnetNode(std::uint32_t playerId, bool isHost, QHostAddress bindAddress, quint16 bindPort, bool proactiveResend) :
    m_playerId(playerId),
    m_hostPlayerId(isHost ? playerId : 0u),
    m_proactiveResendEnabled(proactiveResend)
{
    qInfo() << "[TafnetNode::TafnetNode] proactiveResend=" << proactiveResend;

    m_lobbySocket.bind(bindAddress, bindPort);
    qInfo() << "[TafnetNode::TafnetNode] playerId" << m_playerId << "udp binding to" << m_lobbySocket.localAddress().toString() << ":" << m_lobbySocket.localPort();
    QObject::connect(&m_lobbySocket, &QUdpSocket::readyRead, this, &TafnetNode::onReadyRead);

    QObject::connect(&m_resendTimer, &QTimer::timeout, this, &TafnetNode::onResendTimer);
    m_resendTimer.start(500);

    QObject::connect(&m_resendReqReenableTimer, &QTimer::timeout, this, &TafnetNode::onResendReqReenableTimer);
    m_resendReqReenableTimer.start(666);
}

void TafnetNode::onResendTimer()
{
    for (auto &pairPlayer : m_sendBuffer)
    {
        std::uint32_t peerPlayerId = pairPlayer.first;
        DataBuffer &sendBuffer = pairPlayer.second;

        int maxResendAtOnce = 5;
        for (auto &pairPayload : sendBuffer.getAll())
        {
            std::uint32_t seq = pairPayload.first;
            Payload &data = pairPayload.second;
            if (data.buf)
            {
                int nRepeats = m_resendRates[peerPlayerId].get(true);
                sendMessage(peerPlayerId, data.action, seq, data.buf->data(), data.buf->size(), nRepeats);
            }
            if (--maxResendAtOnce <= 0)
            {
                break;
            }
        }
    }
}

void TafnetNode::onResendReqReenableTimer()
{
    for (auto &pair : m_resendRequestEnabled)
    {
        pair.second.value = true;
    }
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
            qInfo() << "[TafnetNode::onReadyRead] playerId" << m_playerId << "ERROR unexpected message from" << senderAddress.toString() << ":" << senderPort;
            continue;
        }
        std::uint32_t peerPlayerId = it->second;

        const TafnetMessageHeader* tafheader = (TafnetMessageHeader*)datas.data();
        const TafnetBufferedHeader* tafBufferedHeader = (TafnetBufferedHeader*)datas.data();

        DataBuffer &tcpReceiveBuffer = m_receiveBuffer[peerPlayerId];
        DataBuffer &tcpSendBuffer = m_sendBuffer[peerPlayerId];
        bool &resendRequestEnabled = m_resendRequestEnabled[peerPlayerId].value;

        if (tafBufferedHeader->action == Payload::ACTION_TCP_ACK)
        {
            if (tcpSendBuffer.ackData(tafBufferedHeader->seq))
            {
                m_resendRates[peerPlayerId].ackCount++;
            }
        }

        else if (tafBufferedHeader->action == Payload::ACTION_TCP_RESEND)
        {
            std::uint32_t seq = tafBufferedHeader->seq;
            Payload data = tcpSendBuffer.get(seq);
            if (data.buf)
            {
                int nRepeats = m_resendRates[peerPlayerId].get(true);
                qInfo() << "[TafnetNode::onReadyRead] playerId" << m_playerId << "- peer" << peerPlayerId << "requested resend packet" << seq << "resendrate=" <<  nRepeats;
                sendMessage(peerPlayerId, data.action, seq, data.buf->data(), data.buf->size(), nRepeats);
            }
            else
            {
                qWarning() << "[TafnetNode::onReadyRead] no payload found for seq number" << seq;
            }
        }

        else if (tafBufferedHeader->action >= Payload::ACTION_TCP_DATA)
        {
            // received data that requires ACK
            tcpReceiveBuffer.insert(
                tafBufferedHeader->seq, tafBufferedHeader->action,
                datas.data() + sizeof(TafnetBufferedHeader), datas.size() - sizeof(TafnetBufferedHeader));

            int nRepeats = m_resendRates[peerPlayerId].get(false);
            sendMessage(peerPlayerId, Payload::ACTION_TCP_ACK, tafBufferedHeader->seq, "", 0, nRepeats);

            QByteArray &reassemblyBuffer = m_reassemblyBuffer[peerPlayerId];
            while (tcpReceiveBuffer.readyRead())
            {
                resendRequestEnabled = true;    // is also reenabled on a timer
                // clear receive buffer and acknowledge receipt
                std::uint32_t seq = tcpReceiveBuffer.nextExpectedPopSeq();
                Payload data = tcpReceiveBuffer.pop();
                reassemblyBuffer += *data.buf;
                if (data.action != Payload::ACTION_MORE)
                {
                    handleMessage(data.action, peerPlayerId, reassemblyBuffer.data(), reassemblyBuffer.size());
                    reassemblyBuffer.clear();
                }
            }

            if (!tcpReceiveBuffer.empty() && resendRequestEnabled)
            {
                resendRequestEnabled = false;    // is also reenabled on a timer
                int remainingMaxResend = 10;
                for (std::uint32_t seq = tcpReceiveBuffer.nextExpectedPopSeq();
                    seq < tcpReceiveBuffer.earliestAvailable() && remainingMaxResend > 0;
                    ++seq, --remainingMaxResend)
                {
                    qInfo() << "[TafnetNode::onReadyRead] playerId" << m_playerId << "- req resend packet" << seq << "from peer" << peerPlayerId;
                    sendMessage(peerPlayerId, Payload::ACTION_TCP_RESEND, seq, "", 0, nRepeats);
                }
            }
        }

        else
        {
            // received data not requiring ACK
            if (!m_udpDuplicateDetection.isLikelyDuplicate(peerPlayerId, 0, datas.data(), datas.size()))
            {
                handleMessage(tafheader->action, peerPlayerId, datas.data() + sizeof(TafnetMessageHeader), datas.size() - sizeof(TafnetMessageHeader));
            }
        }
    }
}

void TafnetNode::handleMessage(std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len)
{
    m_handleMessage(action, peerPlayerId, data, len);
}

void TafnetNode::setHandler(const std::function<void(std::uint8_t, std::uint32_t, char*, int)>& f)
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
    qInfo() << "[TafnetNode::connectToPeer] playerId" << m_playerId << "connecting to" << peer.toString() << ":" << peerPort << peerPlayerId;
    HostAndPort hostAndPort(peer, peerPort);
    m_peerAddresses[peerPlayerId] = hostAndPort;
    m_peerPlayerIds[hostAndPort] = peerPlayerId;
    sendMessage(peerPlayerId, Payload::ACTION_HELLO, 0, "HELLO", 5, 1);
}

void TafnetNode::disconnectFromPeer(std::uint32_t peerPlayerId)
{
    qInfo() << "[TafnetNode::disconnectFromPeer] playerId" << m_playerId << "disconnecting from" << peerPlayerId;
    auto it = m_peerAddresses.find(peerPlayerId);
    if (it != m_peerAddresses.end())
    {
        m_peerPlayerIds.erase(it->second);
        m_peerAddresses.erase(peerPlayerId);
    }
    m_receiveBuffer.erase(peerPlayerId);
    m_sendBuffer.erase(peerPlayerId);
}

void TafnetNode::sendMessage(std::uint32_t destPlayerId, std::uint32_t action, std::uint32_t seq, const char* data, int len, int nRepeats)
{
    auto it = m_peerAddresses.find(destPlayerId);
    if (it == m_peerAddresses.end())
    {
        qInfo() << "[TafnetNode::sendMessage] playerId" << m_playerId << "ERROR peer" << destPlayerId << "not known";
        return;
    }
    HostAndPort& hostAndPort = it->second;

    QByteArray buf;
    if (action >= Payload::ACTION_TCP_DATA)
    {
        buf.resize(sizeof(TafnetBufferedHeader) + len);
        TafnetBufferedHeader* header = (TafnetBufferedHeader*)buf.data();
        header->action = action;
        header->seq = seq;
        std::memcpy(header+1, data, len);
    }
    else
    {
        buf.resize(sizeof(TafnetMessageHeader) + len);
        TafnetMessageHeader* header = (TafnetMessageHeader*)buf.data();
        header->action = action;
        std::memcpy(header+1, data, len);
    }

    if (!m_proactiveResendEnabled)
    {
        nRepeats = 1;
    }
    for (int n = 0; n < nRepeats; ++n)
    {
#ifdef SIM_PACKET_LOSS
        if (distribution(generator) > SIM_PACKET_LOSS)
#endif
        {
            m_lobbySocket.writeDatagram(buf.data(), buf.size(), QHostAddress(hostAndPort.ipv4addr), hostAndPort.port);
            m_lobbySocket.flush();
        }
    }
}

void TafnetNode::forwardGameData(std::uint32_t destPlayerId, std::uint32_t action, const char* data, int len)
{
    if (m_peerAddresses.count(destPlayerId) == 0)
    {
        return;
    }

    if (action >= Payload::ACTION_TCP_DATA)
    {
        DataBuffer &sendBuffer = m_sendBuffer[destPlayerId];

        static const int MAX_PACKET_SIZE = 500;
        for (int fragOffset = 0; fragOffset < len; fragOffset += MAX_PACKET_SIZE)
        {
            const char *p = data + fragOffset;
            int sz = std::min(MAX_PACKET_SIZE, len - fragOffset);

            int nRepeats = m_resendRates[destPlayerId].get(true);
            if (fragOffset + sz >= len)
            {
                std::uint32_t seq = sendBuffer.push_back(action, p, sz);
                sendMessage(destPlayerId, action, seq, p, sz, nRepeats);
            }
            else
            {
                std::uint32_t seq = sendBuffer.push_back(Payload::ACTION_MORE, p, sz);
                sendMessage(destPlayerId, Payload::ACTION_MORE, seq, p, sz, nRepeats);
            }
        }
    }
    else
    {
        int nRepeats = m_resendRates[destPlayerId].get(false);
        sendMessage(destPlayerId, action, 0, data, len, nRepeats);
    }
}

void TafnetNode::resetTcpBuffers()
{
    for (auto &buf : m_receiveBuffer)
    {
        buf.second.reset();
    }
    for (auto &buf : m_sendBuffer)
    {
        buf.second.reset();
    }
}
