#include "TafnetNode.h"
#include "taflib/Watchdog.h"

#include <QtNetwork/qtcpsocket.h>
#include <QtCore/qdatetime.h>

#include <cstring>

#ifdef _DEBUG
#include <tademo/HexDump.h>
#endif

//#define SIM_PACKET_LOSS 50
//#define SIM_PACKET_TRUNCATE 800
//#define SIM_PACKET_ERROR_LARGER_THAN 400

#ifdef SIM_PACKET_LOSS
#include <random>
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(0, 99);
#endif

using namespace tafnet;

Payload::Payload():
action(ACTION_INVALID),
timestamp(0)
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
    timestamp = QDateTime::currentMSecsSinceEpoch();
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

TafnetNode::ResendRate::ResendRate() :
    timestampLastPing(0),
    timestampLastPingAck(0),
    timestampFirstPing(0),
    lastTimeoutSeq(0),
    lastResendReqSeq(0)
{ }

int TafnetNode::ResendRate::getResendRate(bool incSendCount)
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

void TafnetNode::ResendRate::registerAck()
{
    timestampLastPingAck = QDateTime::currentMSecsSinceEpoch();
    if (timestampLastPing > 0 && timestampLastPingAck - timestampLastPing > 0)
    {
        recentPings.push_back(timestampLastPingAck - timestampLastPing);
        while (recentPings.size() > RECENT_PING_BUFFER_SIZE)
        {
            recentPings.pop_front();
        }
    }
}

std::int64_t TafnetNode::ResendRate::getSuccessfulPingTime()
{
    if (recentPings.size() > 0u)
    {
        std::vector<std::int64_t> pings(recentPings.begin(), recentPings.end());
        auto it = pings.begin() + recentPings.size()-1;
        std::nth_element(pings.begin(), it, pings.end());
        return *it;
    }
    else
    {
        return -1;
    }
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

TafnetNode::TafnetNode(std::uint32_t playerId, bool isHost, QHostAddress bindAddress, quint16 bindPort, bool proactiveResend, std::uint32_t maxPacketSize) :
    m_playerId(playerId),
    m_hostPlayerId(isHost ? playerId : 0u),
    m_maxPacketSize(maxPacketSize),
    m_proactiveResendEnabled(proactiveResend)
{
    m_crc32.Initialize();

    qInfo() << "[TafnetNode::TafnetNode] proactiveResend=" << proactiveResend;
    qInfo() << "[TafnetNode::TafnetNode] sizeof(TafnetMessageHeader)=" << sizeof(TafnetMessageHeader);
    qInfo() << "[TafnetNode::TafnetNode] sizeof(TafnetBufferedHeader)=" << sizeof(TafnetBufferedHeader);
#ifdef SIM_PACKET_TRUNCATE
    qInfo() << "[TafnetNode::TafnetNode] SIM_PACKET_TRUNCATE=" << SIM_PACKET_TRUNCATE;
#endif
#ifdef SIM_PACKET_ERROR_LARGER_THAN
    qInfo() << "[TafnetNode::TafnetNode] SIM_PACKET_ERROR_LARGER_THAN=" << SIM_PACKET_ERROR_LARGER_THAN;
#endif

    m_lobbySocket.bind(bindAddress, bindPort);
    qInfo() << "[TafnetNode::TafnetNode] playerId" << m_playerId << "udp binding to" << m_lobbySocket.localAddress().toString() << ":" << m_lobbySocket.localPort();
    QObject::connect(&m_lobbySocket, &QUdpSocket::readyRead, this, &TafnetNode::onReadyRead);

    QObject::connect(&m_resendTimer, &QTimer::timeout, this, &TafnetNode::onResendTimer);
    m_resendTimer.start(RESEND_TIMER_INTERVAL);

    QObject::connect(&m_resendReqReenableTimer, &QTimer::timeout, this, &TafnetNode::onResendReqReenableTimer);
    m_resendReqReenableTimer.start(RESEND_TIMER_HOLD_OFF_TIME);
}

void TafnetNode::onResendTimer()
{
    try
    {
        taflib::Watchdog wd("TafnetNode::onResendTimer", 100);
        for (auto &pairPlayer : m_sendBuffer)
        {
            std::uint32_t peerPlayerId = pairPlayer.first;
            ResendRate& stats = m_resendRates[peerPlayerId];
            DataBuffer &sendBuffer = pairPlayer.second;
            int expectedPing = stats.getSuccessfulPingTime();
            int timeout = expectedPing > 0 ? 11 * expectedPing / 10 + RESEND_TIMEOUT_MARGIN : INITIAL_RESEND_TIMEOUT;
            timeout = std::min(MAX_RESEND_TIMEOUT, timeout);

            int maxResendAtOnce = MAX_RESEND_AT_ONCE;
            qint64 tNow = QDateTime::currentMSecsSinceEpoch();
            for (auto &pairPayload : sendBuffer.getAll())
            {
                std::uint32_t seq = pairPayload.first;
                Payload &data = pairPayload.second;
                if (tNow < data.timestamp + timeout)
                {
                    break;
                }
                if (data.buf)
                {
                    int nRepeats = stats.getResendRate(true);
                    sendMessage(peerPlayerId, data.action, seq, data.buf->data(), data.buf->size(), nRepeats);
                }
                if (seq > stats.lastTimeoutSeq)
                {
                    qInfo() << "[TafnetNode::onResendTimer] ACK timeout on player" << peerPlayerId << "seq" << seq << "expectedPing=" << expectedPing << "timeout=" << timeout;
                    stats.lastTimeoutSeq = seq;
                }
                if (--maxResendAtOnce <= 0)
                {
                    break;
                }
            }
        }
    }
    catch (std::exception &e)
    {
        qWarning() << "[TafnetNode::onResendTimer] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TafnetNode::onResendTimer] unknown exception";
    }
}

void TafnetNode::onResendReqReenableTimer()
{
    try
    {
        taflib::Watchdog wd("TafnetNode::onResendReqReenableTimer", 100);
        for (auto &pair : m_resendRequestEnabled)
        {
            pair.second.value = true;
        }
    }
    catch (std::exception &e)
    {
        qWarning() << "[TafnetNode::onResendReqReenableTimer] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TafnetNode::onResendReqReenableTimer] unknown exception";
    }
}

void TafnetNode::onReadyRead()
{
    try
    {
        taflib::Watchdog wd("TafnetNode::onReadyRead", 100);
        QUdpSocket* sender = static_cast<QUdpSocket*>(QObject::sender());

        while (sender->hasPendingDatagrams())
        {
            taflib::Watchdog wd("TafnetNode::onReadyRead while hasPendingDatagrams()", 100);
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

#ifdef SIM_PACKET_TRUNCATE
            datas.resize(std::min(datas.size(), SIM_PACKET_TRUNCATE));
#endif

#ifdef SIM_PACKET_ERROR_LARGER_THAN
            if (datas.size() > SIM_PACKET_ERROR_LARGER_THAN)
            {
                *datas.rbegin() ^= 0xff;
            }
#endif

            const TafnetMessageHeader* tafheader = (TafnetMessageHeader*)datas.data();
            const TafnetBufferedHeader* tafBufferedHeader = (TafnetBufferedHeader*)datas.data();

            std::uint32_t peerPlayerId = 0;//tafheader->senderId;

            // we prefer to use m_peerPlayerIds to lookup sender's playerId
            // but for some reason readDatagram doesn't set senderAddress and senderPort when run on linux using wine ...
            // seems ok for native linux tho
            if (true) {
                auto it = m_peerPlayerIds.find(senderHostAndPort);
                if (it == m_peerPlayerIds.end())
                {
                    qInfo() << "[TafnetNode::onReadyRead] ERROR unexpected message from" << senderAddress.toString() << ":" << senderPort;
                    continue;
                }
                peerPlayerId = it->second;
            }

            DataBuffer &tcpReceiveBuffer = m_receiveBuffer[peerPlayerId];
            DataBuffer &tcpSendBuffer = m_sendBuffer[peerPlayerId];
            bool &resendRequestEnabled = m_resendRequestEnabled[peerPlayerId].value;

            if (tafBufferedHeader->action == Payload::ACTION_TCP_ACK)
            {
                taflib::Watchdog wd("TafnetNode::onReadyRead TCP_ACK", 100);
                if (tcpSendBuffer.ackData(tafBufferedHeader->seq))
                {
                    m_resendRates[peerPlayerId].ackCount++;
                }
            }

            else if (tafBufferedHeader->action == Payload::ACTION_TCP_RESEND)
            {
                taflib::Watchdog wd("TafnetNode::onReadyRead TCP_RESEND", 100);
                std::uint32_t seq = tafBufferedHeader->seq;
                Payload data = tcpSendBuffer.get(seq);
                if (data.buf)
                {
                    taflib::Watchdog wd("TafnetNode::onReadyRead TCP_RESEND data.buf", 100);
                    ResendRate &stats = m_resendRates[peerPlayerId];
                    int nRepeats = stats.getResendRate(true);
                    if (seq > stats.lastResendReqSeq)
                    {
                        qInfo() << "[TafnetNode::onReadyRead] peer" << peerPlayerId << "requested resend packet" << seq << "resendrate=" << nRepeats;
                        stats.lastResendReqSeq = seq;
                    }
                    sendMessage(peerPlayerId, data.action, seq, data.buf->data(), data.buf->size(), nRepeats);
                }
                else
                {
                    qWarning() << "[TafnetNode::onReadyRead] no payload found for seq number" << seq;
                }
            }

            else if (tafBufferedHeader->action == Payload::ACTION_PACKSIZE_TEST)
            {
                std::uint32_t testPacketSize = tafBufferedHeader->seq;
                if (datas.size() == testPacketSize + sizeof(TafnetBufferedHeader))
                {
                    taflib::Watchdog wd("TafnetNode::onReadyRead PACKSIZE_TEST", 100);
                    const std::uint32_t *testPacketCrc = (std::uint32_t*)(tafBufferedHeader + 1);
                    const unsigned char *testPacketData = (const unsigned char*)(testPacketCrc + 1);
                    const std::uint32_t crc = m_crc32.FullCRC(testPacketData, testPacketSize - sizeof(std::uint32_t));
                    if (crc == *testPacketCrc)
                    {
                        taflib::Watchdog wd("TafnetNode::onReadyRead PACKSIZE_TEST send", 100);
                        if (testPacketSize <= m_maxPacketSize)
                        {
                            if (testPacketSize > PING_PACKET_SIZE)
                            {
                                qInfo() << "[TafnetNode::onReadyRead] ACTION_PACKSIZE_TEST peer=" << peerPlayerId << "packsize = " << testPacketSize;
                            }
                            sendMessage(peerPlayerId, Payload::ACTION_PACKSIZE_ACK, tafBufferedHeader->seq, "", 0, 1);
                        }
                        else
                        {
                            qInfo() << "[TafnetNode::onReadyRead] ACTION_PACKSIZE_TEST peer=" << peerPlayerId << "packsize = " << testPacketSize << " exceeds our maxPacketSize. quietly ignoring ...";
                        }
                    }
                    else
                    {
                        qWarning() << "[TafnetNode::onReadyRead] ACTION_PACKSIZE_TEST peer=" << peerPlayerId << "packsize = " << testPacketSize << "crc error";
                    }
                }
                else
                {
                    qWarning() << "[TafnetNode::onReadyRead] ACTION_PACKSIZE_TEST peer=" << peerPlayerId << "packsize=" << testPacketSize << "mismatch. received=" << datas.size();
                }
            }

            else if (tafBufferedHeader->action == Payload::ACTION_PACKSIZE_ACK)
            {
                taflib::Watchdog wd("TafnetNode::onReadyRead PACKSIZE_ACK", 100);
                ResendRate& stats = m_resendRates[peerPlayerId];
                stats.registerAck();
                std::uint32_t ackedPacketSize = tafBufferedHeader->seq;
                if (ackedPacketSize > stats.maxPacketSize)
                {
                    qInfo() << "[TafnetNode::onReadyRead] ACTION_PACKSIZE_ACK peer=" << peerPlayerId << "packsize=" << ackedPacketSize << "setting new maximum";
                    stats.maxPacketSize = ackedPacketSize;
                }
            }

            else if (tafBufferedHeader->action >= Payload::ACTION_TCP_DATA)
            {
                // received data that requires ACK
                taflib::Watchdog wd("TafnetNode::onReadyRead >=TCP_DATA", 100);
                tcpReceiveBuffer.insert(
                    tafBufferedHeader->seq, tafBufferedHeader->action,
                    datas.data() + sizeof(TafnetBufferedHeader), datas.size() - sizeof(TafnetBufferedHeader));

                int nRepeats = m_resendRates[peerPlayerId].getResendRate(false);
                sendMessage(peerPlayerId, Payload::ACTION_TCP_ACK, tafBufferedHeader->seq, "", 0, nRepeats);

                QByteArray &reassemblyBuffer = m_reassemblyBuffer[peerPlayerId];
                while (tcpReceiveBuffer.readyRead())
                {
                    taflib::Watchdog wd("TafnetNode::onReadyRead >=TCP_DATA while tcpReceiveBuffer", 100);
                    resendRequestEnabled = true;    // is also reenabled on a timer
                    // clear receive buffer and acknowledge receipt
                    std::uint32_t seq = tcpReceiveBuffer.nextExpectedPopSeq();
                    Payload data = tcpReceiveBuffer.pop();
                    reassemblyBuffer += *data.buf;
                    if (data.action != Payload::ACTION_MORE)
                    {
                        taflib::Watchdog wd("TafnetNode::onReadyRead >=TCP_DATA handleMessage", 100);
                        handleMessage(data.action, peerPlayerId, reassemblyBuffer.data(), reassemblyBuffer.size());
                        reassemblyBuffer.clear();
                    }
                }

                if (!tcpReceiveBuffer.empty() && resendRequestEnabled)
                {
                    taflib::Watchdog wd("TafnetNode::onReadyRead >=TCP_DATA !tcpReceiveBuffer.empty", 100);
                    resendRequestEnabled = false;    // is also reenabled on a timer
                    int remainingMaxResend = 10;
                    for (std::uint32_t seq = tcpReceiveBuffer.nextExpectedPopSeq();
                        seq < tcpReceiveBuffer.earliestAvailable() && remainingMaxResend > 0;
                        ++seq, --remainingMaxResend)
                    {
                        qInfo() << "[TafnetNode::onReadyRead] req resend packet" << seq << "from peer" << peerPlayerId;
                        sendMessage(peerPlayerId, Payload::ACTION_TCP_RESEND, seq, "", 0, nRepeats);
                    }
                }
            }

            else
            {
                // received data not requiring ACK
                taflib::Watchdog wd("TafnetNode::onReadyRead other data", 100);
                if (!m_udpDuplicateDetection.isLikelyDuplicate(peerPlayerId, 0, datas.data(), datas.size()))
                {
                    taflib::Watchdog wd("TafnetNode::onReadyRead other data not duplicate", 100);
                    handleMessage(tafheader->action, peerPlayerId, datas.data() + sizeof(TafnetMessageHeader), datas.size() - sizeof(TafnetMessageHeader));
                }
            }
        }
    }
    catch (std::exception &e)
    {
        qWarning() << "[TafnetNode::onReadyRead] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TafnetNode::onReadyRead] unknown exception";
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

std::uint32_t TafnetNode::maxPacketSizeForPlayerId(std::uint32_t id) const
{
    auto it = m_resendRates.find(id);
    if (it == m_resendRates.end())
    {
        return MAX_PACKET_SIZE_LOWER_LIMIT;
    }
    else
    {
        return it->second.maxPacketSize;
    }
}

void TafnetNode::joinGame(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId)
{
    connectToPeer(peer, peerPort, peerPlayerId);
    m_hostPlayerId = peerPlayerId;
}

void TafnetNode::connectToPeer(QHostAddress peer, quint16 peerPort, std::uint32_t peerPlayerId)
{
    qInfo() << "[TafnetNode::connectToPeer] connecting to" << peer.toString() << ":" << peerPort << peerPlayerId;
    HostAndPort hostAndPort(peer, peerPort);
    m_peerAddresses[peerPlayerId] = hostAndPort;
    m_peerPlayerIds[hostAndPort] = peerPlayerId;

    if (m_receiveBuffer.count(peerPlayerId) || m_sendBuffer.count(peerPlayerId))
    {
        qInfo() << "[TafnetNode::connectToPeer] pre-existing send/receive buffers for peerPlayerId=" << peerPlayerId << ".  Cleaning up ...";
    }
    m_receiveBuffer.erase(peerPlayerId);
    m_sendBuffer.erase(peerPlayerId);
    m_reassemblyBuffer.erase(peerPlayerId);
    m_resendRates.erase(peerPlayerId);
    m_resendRequestEnabled.erase(peerPlayerId);
    forwardGameData(peerPlayerId, Payload::ACTION_HELLO, "HELLO", 5);
}

void TafnetNode::disconnectFromPeer(std::uint32_t peerPlayerId)
{
    qInfo() << "[TafnetNode::disconnectFromPeer] disconnecting from" << peerPlayerId;
    auto it = m_peerAddresses.find(peerPlayerId);
    if (it != m_peerAddresses.end())
    {
        m_peerPlayerIds.erase(it->second);
    }
    m_peerAddresses.erase(peerPlayerId);
    m_receiveBuffer.erase(peerPlayerId);
    m_sendBuffer.erase(peerPlayerId);
    m_reassemblyBuffer.erase(peerPlayerId);
    m_resendRates.erase(peerPlayerId);
    m_resendRequestEnabled.erase(peerPlayerId);
}

void TafnetNode::sendMessage(std::uint32_t destPlayerId, std::uint32_t action, std::uint32_t seq, const char* data, int len, int nRepeats)
{
    auto it = m_peerAddresses.find(destPlayerId);
    if (it == m_peerAddresses.end())
    {
        qInfo() << "[TafnetNode::sendMessage] ERROR peer" << destPlayerId << "not known";
        return;
    }
    HostAndPort& hostAndPort = it->second;

    QByteArray buf;
    if (action >= Payload::ACTION_TCP_DATA)
    {
        buf.resize(sizeof(TafnetBufferedHeader) + len);
        TafnetBufferedHeader* header = (TafnetBufferedHeader*)buf.data();
        //header->senderId = this->getPlayerId();
        header->action = action;
        header->seq = seq;
        std::memcpy(header+1, data, len);
    }
    else
    {
        buf.resize(sizeof(TafnetMessageHeader) + len);
        TafnetMessageHeader* header = (TafnetMessageHeader*)buf.data();
        //header->senderId = this->getPlayerId();
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

void TafnetNode::forwardGameData(std::uint32_t destPlayerId, std::uint32_t action, const char* data, int _len)
{
    taflib::Watchdog wd("TafnetNode::forwardGameData", 100);
    const unsigned len = (unsigned)_len;
    if (m_peerAddresses.count(destPlayerId) == 0)
    {
        return;
    }

    if (action >= Payload::ACTION_TCP_DATA)
    {
        DataBuffer &sendBuffer = m_sendBuffer[destPlayerId];
        std::uint32_t maxPacketSize = m_resendRates[destPlayerId].maxPacketSize;

        unsigned numFragments = len / (maxPacketSize+1) + 1;
        maxPacketSize = (len+numFragments-1) / numFragments;
        maxPacketSize = std::min(maxPacketSize, m_maxPacketSize);
        maxPacketSize = std::max(maxPacketSize, MAX_PACKET_SIZE_LOWER_LIMIT);

        for (std::uint32_t fragOffset = 0u; fragOffset < len; fragOffset += maxPacketSize)
        {
            const char *p = data + fragOffset;
            int sz = std::min(maxPacketSize, len - fragOffset);

            int nRepeats = m_resendRates[destPlayerId].getResendRate(true);
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
        int nRepeats = m_resendRates[destPlayerId].getResendRate(false);
        sendMessage(destPlayerId, action, 0, data, len, nRepeats);
    }
}

void TafnetNode::sendPacksizeTests(std::uint32_t peerPlayerId)
{
    taflib::Watchdog wd("TafnetNode::sendPacksizeTests", 100);
    std::vector<char> _testData(m_maxPacketSize);
    char* testData = _testData.data();
    for (unsigned n = 0; n < m_maxPacketSize; ++n)
    {
        testData[n] = (char)n;
    }

    std::uint32_t sz = m_resendRates[peerPlayerId].maxPacketSize = MAX_PACKET_SIZE_LOWER_LIMIT;
    for (;;)
    {
        *(std::uint32_t*)testData = m_crc32.FullCRC((unsigned char*)testData + sizeof(std::uint32_t), sz - sizeof(std::uint32_t));
        qInfo() << "[TafnetNode::sendPacksizeTests] sending ACTION_PACKSIZE_TEST to peer=" << peerPlayerId << "packsize=" << sz;
        sendMessage(peerPlayerId, Payload::ACTION_PACKSIZE_TEST, sz, testData, sz, 3);

        if (sz >= m_maxPacketSize)
        {
            break;
        }
        sz = std::min(1412 * sz / 1000, m_maxPacketSize);
    }
    m_resendRates[peerPlayerId].timestampLastPing = QDateTime::currentMSecsSinceEpoch();
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

void TafnetNode::sendPingToPeers()
{
    std::vector<char> _testData(PING_PACKET_SIZE);
    char* testData = _testData.data();
    for (unsigned n = 0; n < PING_PACKET_SIZE; ++n)
    {
        testData[n] = (char)n;
    }

    *(std::uint32_t*)testData = m_crc32.FullCRC((unsigned char*)testData + sizeof(std::uint32_t), PING_PACKET_SIZE - sizeof(std::uint32_t));

    std::set<std::uint32_t> lostPeerIds;
    for (auto it = m_peerPlayerIds.begin(); it != m_peerPlayerIds.end(); ++it)
    {
        std::uint32_t peerId = it->second;
        ResendRate& stats = m_resendRates[peerId];
        std::int64_t tNow = QDateTime::currentMSecsSinceEpoch();
        if (stats.timestampLastPingAck > 0 && tNow - stats.timestampLastPingAck > DEAD_PEER_TIMEOUT)
        {
            lostPeerIds.insert(peerId);
            qInfo() << "[sendPingToPeers] peerId=" << peerId << "has stopped responding.  Considering them a lost peer.";
            continue;
        }
        else if (stats.timestampLastPingAck == 0 && stats.timestampFirstPing > 0 && tNow - stats.timestampFirstPing > DEAD_PEER_TIMEOUT)
        {
            lostPeerIds.insert(peerId);
            qInfo() << "[sendPingToPeers] peerId=" << peerId << "never responded.  Considering them a lost peer.";
            continue;
        }

        if (stats.timestampFirstPing == 0)
        {
            stats.timestampFirstPing = tNow;
        }
        stats.timestampLastPing = tNow;
        sendMessage(peerId, Payload::ACTION_PACKSIZE_TEST, PING_PACKET_SIZE, testData, PING_PACKET_SIZE, 1);
    }

    for (std::uint32_t peerId : lostPeerIds)
    {
        disconnectFromPeer(peerId);
    }
}

std::map<std::uint32_t, std::int64_t> TafnetNode::getPingToPeers()
{
    std::map<std::uint32_t, std::int64_t> results;
    for (auto it = m_resendRates.begin(); it != m_resendRates.end(); ++it)
    {
        std::uint32_t peerId = it->first;
        if (it->second.timestampLastPing > 0)
        {
            if (it->second.timestampLastPingAck < it->second.timestampLastPing)
            {
                results[peerId] = QDateTime::currentMSecsSinceEpoch() - it->second.timestampLastPing;
            }
            else
            {
                results[peerId] = it->second.timestampLastPingAck - it->second.timestampLastPing;
            }
        }
    }
    return results;
}
