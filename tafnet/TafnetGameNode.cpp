#include "TafnetGameNode.h"
#include "GameAddressTranslater.h"
#include "GetUpnpPortMap.h"
#include "tademo/HexDump.h"

#include <sstream>

using namespace tafnet;


GameSender* TafnetGameNode::getGameSender(std::uint32_t remoteTafnetId)
{
    std::shared_ptr<GameSender>& gameSender = m_gameSenders[remoteTafnetId];
    if (!gameSender)
    {
        gameSender.reset(m_gameSenderFactory());
        gameSender->setTcpPort(m_gameTcpPort);
        gameSender->setUdpPort(m_gameUdpPort);
    }
    return gameSender.get();
}

void TafnetGameNode::killGameSender(std::uint32_t remoteTafnetId)
{
    m_gameSenders.erase(remoteTafnetId);
}

GameReceiver* TafnetGameNode::getGameReceiver(std::uint32_t remoteTafnetId, QSharedPointer<QUdpSocket> udpSocket)
{
    std::shared_ptr<GameReceiver>& gameReceiver = m_gameReceivers[remoteTafnetId];
    if (!gameReceiver)
    {
        gameReceiver.reset(m_gameReceiverFactory(udpSocket));
        if (m_remotePlayerIds.count(gameReceiver->getTcpListenPort()) == 0) m_remotePlayerIds[gameReceiver->getTcpListenPort()] = remoteTafnetId;
        if (m_remotePlayerIds.count(gameReceiver->getUdpListenPort()) == 0) m_remotePlayerIds[gameReceiver->getUdpListenPort()] = remoteTafnetId;
        if (m_remotePlayerIds.count(gameReceiver->getEnumListenPort()) == 0) m_remotePlayerIds[gameReceiver->getEnumListenPort()] = remoteTafnetId;
        gameReceiver->setHandler([this](QAbstractSocket* receivingSocket, int channelCode, char* data, int len) {
            this->handleGameData(receivingSocket, channelCode, data, len);
        });
    }
    return gameReceiver.get();
}

void TafnetGameNode::killGameReceiver(std::uint32_t remoteTafnetId)
{
    auto it = m_gameReceivers.find(remoteTafnetId);
    if (it != m_gameReceivers.end())
    {
        m_remotePlayerIds.erase(it->second->getTcpListenPort());
        m_remotePlayerIds.erase(it->second->getUdpListenPort());
        if (m_gameReceivers.size() == 1)
        {
            m_remotePlayerIds.erase(it->second->getEnumListenPort());
        }
        m_gameReceivers.erase(remoteTafnetId);
    }
}

void TafnetGameNode::translateMessageFromLocalGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[])
{
    // message from game will include SP Addresses with locally visible address.
    // We need to translate them into playerIds so the remote Tafnet nodes can
    // substitute them back with their own local address/port for the respective player
    GameAddressTranslater tx(replyAddress, replyPorts, [this](DPAddress& address, int index) {
        auto itPlayerId = m_remotePlayerIds.find(address.port());
        if (itPlayerId == m_remotePlayerIds.end())
        {
            address.address(this->m_tafnetNode->getPlayerId());
        }
        else
        {
            address.address(itPlayerId->second);
        }
        return true;
    });
    tx(data, len);
}


void TafnetGameNode::handleGameData(QAbstractSocket* receivingSocket, int channelCode, char* data, int len)
{
    //qInfo() << "[TafnetGameNode::handleGameData] recievePort=" << receivingSocket->localPort() << "channelCode=" << channelCode << "len=" << len;
    if (m_remotePlayerIds.count(receivingSocket->localPort()) == 0)
    {
        return;
    }

    std::uint32_t destNodeId = m_remotePlayerIds[receivingSocket->localPort()];
    if (destNodeId == 0)
    {
        qInfo() << "[TafnetGameNode::handleGameData] ERROR: unable to determine destination tafnetid for game data received on port" << receivingSocket->localPort();
        return;
    }

    if (m_gameTcpPort == 0 || m_gameUdpPort == 0)
    {
        updateGameSenderPorts(data, len);
    }

    if (channelCode == GameReceiver::CHANNEL_UDP)
    {
        m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_UDP_DATA, data, len);
    }

    else if (channelCode == GameReceiver::CHANNEL_TCP)
    {
        // our local ports are meaningless to the remote peer
        // what is important is the playerId that translateMessageFromLocalGame substitutes into the address field
        static const quint16 dummyports[] = { 0xdead, 0xbeef };
        translateMessageFromLocalGame(data, len, 0, dummyports);
        m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_TCP_DATA, data, len);
    }

    else if (channelCode == GameReceiver::CHANNEL_ENUM)
    {
        qInfo() << "[TafnetGameNode::handleGameData] Forwarding enum session request to" << destNodeId;
        m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_ENUM, data, len);
    }
}


void TafnetGameNode::translateMessageFromRemoteGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[])
{
    GameAddressTranslater tx(replyAddress, replyPorts, [this, replyAddress, replyPorts](DPAddress& address, int index) {
        // messages from Tafnet have player SP addresses substituted with PlayerId. Here we substitute the local GameReceiver's address/port for that player
        std::uint32_t remotePlayerId = address.address();
        auto it = this->m_gameReceivers.find(remotePlayerId);
        if (it != this->m_gameReceivers.end())
        {
            address.address(it->second->getBindAddress().toIPv4Address());
            address.port(index ? it->second->getUdpListenPort() : it->second->getTcpListenPort());
        }
        else
        {
            address.address(replyAddress);
            address.port(replyPorts[index > 0]);
        }
        return true;
    });
    tx(data, len);
}


void TafnetGameNode::handleTafnetMessage(const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len)
{
    GameSender* gameSender = getGameSender(peerPlayerId);
    GameReceiver* gameReceiver = getGameReceiver(peerPlayerId, gameSender->getUdpSocket());
    quint16 replyPorts[2];
    gameReceiver->getListenPorts(replyPorts);

    //qInfo() << "[TafnetGameNode::handleTafnetMessage] me=" << m_tafnetNode->getPlayerId() << "from=" << peerPlayerId << "action=" << tafheader.action;
    switch (tafheader.action)
    {
    case TafnetMessageHeader::ACTION_HELLO:
        // no further action beyond creating a gameSender/Receiver required
        break;

    case TafnetMessageHeader::ACTION_ENUM:
        GameAddressTranslater (gameReceiver->getBindAddress().toIPv4Address(), replyPorts)(data, len);
        gameSender->enumSessions(data, len);
        break;

    case TafnetMessageHeader::ACTION_TCP_OPEN:
        gameSender->openTcpSocket(3);
        break;

    case TafnetMessageHeader::ACTION_TCP_DATA:
        translateMessageFromRemoteGame(data, len, gameReceiver->getBindAddress().toIPv4Address(), replyPorts);
        gameSender->sendTcpData(data, len);
        break;

    case TafnetMessageHeader::ACTION_TCP_CLOSE:
        gameSender->closeTcpSocket();
        break;

    case TafnetMessageHeader::ACTION_UDP_DATA:
        gameSender->sendUdpData(data, len);
        break;

    default:
        qInfo() << "[TafnetGameNode::handleTafnetMessage] ERROR unknown action!";
        break;
    };
}

TafnetGameNode::TafnetGameNode(TafnetNode* tafnetNode, std::function<GameSender * ()> gameSenderFactory, std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> gameReceiverFactory) :
    m_tafnetNode(tafnetNode),
    m_gameTcpPort(0),
    m_gameUdpPort(0),
    m_gameSenderFactory(gameSenderFactory),
    m_gameReceiverFactory(gameReceiverFactory)
{
    m_tafnetNode->setHandler([this](const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len) {
        this->handleTafnetMessage(tafheader, peerPlayerId, data, len);
    });
}

void TafnetGameNode::registerRemotePlayer(std::uint32_t remotePlayerId)
{
    qInfo() << "[TafnetGameNode::registerRemotePlayer]" << m_tafnetNode->getPlayerId() << "registering player" << remotePlayerId;
    GameSender* gameSender = getGameSender(remotePlayerId);
    GameReceiver* gameReceiver = getGameReceiver(remotePlayerId, gameSender->getUdpSocket());
}

void TafnetGameNode::unregisterRemotePlayer(std::uint32_t remotePlayerId)
{
    qInfo() << "[TafnetGameNode::unregisterRemotePlayer]" << m_tafnetNode->getPlayerId() << "unregistering player" << remotePlayerId;
    killGameSender(remotePlayerId);
    killGameReceiver(remotePlayerId);
}

void TafnetGameNode::updateGameSenderPorts(const char* data, int len)
{
    DPHeader* header = (DPHeader*)data;
    if (header->looksOk())
    {
        m_gameTcpPort = header->address.port();
        m_gameUdpPort = header->address.port() + 50;

        if (m_gameTcpPort < 2300 || m_gameUdpPort >= 2400)
        {
            // game is trying to use upnp ...
            qInfo() << "[TafnetGameNode::updateGameSenderPorts] Game is trying to use upnp. external tcp port is" << m_gameTcpPort;
            try
            {
                std::uint16_t internalPort = GetUpnpPortMap(m_gameTcpPort, "TCP");
                qInfo() << "[TafnetGameNode::updateGameSenderPorts] IGD reports corresponding internal TCP port is" << internalPort;
                m_gameTcpPort = internalPort;
                m_gameUdpPort = internalPort + 50;
            }
            catch (std::runtime_error &e)
            {
                qWarning() << "[TafnetGameNode::updateGameSenderPorts] unable to get upnp port mappings:" << e.what();
            }
        }

        if (m_gameTcpPort < 2300 || m_gameUdpPort >= 2400)
        {
            qWarning() << "[TafnetGameNode::updateGameSenderPorts] guessing the game ports ...";
            m_gameTcpPort = 2300;
            m_gameUdpPort = 2350;
        }

        qInfo() << "[TafnetGameNode::updateGameSenderPorts] game ports set to tcp:" << m_gameTcpPort << " udp:" << m_gameUdpPort;
        for (auto& pair : m_gameSenders)
        {
            pair.second->setTcpPort(m_gameTcpPort);
            pair.second->setUdpPort(m_gameUdpPort);
        }
    }
    else
    {
        qWarning() << "[TafnetGameNode::updateGameSenderPorts] cannot set game ports due to unrecognised dplay msg header";
        std::ostringstream ss;
        ss << '\n';
        TADemo::HexDump(data, len, ss);
        qWarning() << ss.str().c_str();
    }
}

void TafnetGameNode::resetGameConnection()
{
    qInfo() << "[TafnetGameNode::resetGameConnection]";
    m_gameTcpPort = m_gameUdpPort = 0;
    for (auto &pair : m_gameSenders)
    {
        pair.second->resetGameConnection();
    }
    for (auto &pair : m_gameReceivers)
    {
        pair.second->resetGameConnection();
    }
}
