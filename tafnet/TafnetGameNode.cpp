#include "TafnetGameNode.h"
#include "GameAddressTranslater.h"

using namespace tafnet;


GameSender* TafnetGameNode::getGameSender(std::uint32_t remoteTafnetId)
{
    std::shared_ptr<GameSender>& gameSender = m_gameSenders[remoteTafnetId];
    if (!gameSender)
    {
        gameSender.reset(m_gameSenderFactory());
    }
    return gameSender.get();
}

GameReceiver* TafnetGameNode::getGameReceiver(std::uint32_t remoteTafnetId, GameSender* sender)
{
    std::shared_ptr<GameReceiver>& gameReceiver = m_gameReceivers[remoteTafnetId];
    if (!gameReceiver)
    {
        gameReceiver.reset(m_gameReceiverFactory(sender));
        if (m_remotePlayerIds.count(gameReceiver->getTcpListenPort()) == 0) m_remotePlayerIds[gameReceiver->getTcpListenPort()] = remoteTafnetId;
        if (m_remotePlayerIds.count(gameReceiver->getUdpListenPort()) == 0) m_remotePlayerIds[gameReceiver->getUdpListenPort()] = remoteTafnetId;
        if (m_remotePlayerIds.count(gameReceiver->getEnumListenPort()) == 0) m_remotePlayerIds[gameReceiver->getEnumListenPort()] = remoteTafnetId;
        gameReceiver->setHandler([this](QAbstractSocket* receivingSocket, int channelCode, char* data, int len) {
            this->handleGameData(receivingSocket, channelCode, data, len);
        });
    }
    return gameReceiver.get();
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
    //qDebug() << "[TafnetGameNode::handleGameData] recievePort=" << receivingSocket->localPort() << "channelCode=" << channelCode << "len=" << len;
    if (m_remotePlayerIds.count(receivingSocket->localPort()) == 0)
    {
        return;
    }

    std::uint32_t destNodeId = m_remotePlayerIds[receivingSocket->localPort()];
    if (destNodeId == 0)
    {
        qDebug() << "ERROR: unable to determine destination tafnetid for game data received on port" << receivingSocket->localPort();
        return;
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
    GameReceiver* gameReceiver = getGameReceiver(peerPlayerId, gameSender);
    quint16 replyPorts[2];
    gameReceiver->getListenPorts(replyPorts);

    //qDebug() << "[TafnetGameNode::handleTafnetMessage] me=" << m_tafnetNode->getPlayerId() << "from=" << peerPlayerId << "action=" << tafheader.action;
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
        qDebug() << "[TafnetGameNode::handleTafnetMessage] ERROR unknown action!";
        break;
    };
}

TafnetGameNode::TafnetGameNode(TafnetNode* tafnetNode, std::function<GameSender * ()> gameSenderFactory, std::function<GameReceiver * (GameSender*)> gameReceiverFactory) :
    m_tafnetNode(tafnetNode),
    m_gameSenderFactory(gameSenderFactory),
    m_gameReceiverFactory(gameReceiverFactory)
{
    m_tafnetNode->setHandler([this](const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len) {
        this->handleTafnetMessage(tafheader, peerPlayerId, data, len);
    });
}

void TafnetGameNode::registerRemotePlayer(std::uint32_t remotePlayerId)
{
    qDebug() << "[TafnetGameNode::registerRemotePlayer]" << m_tafnetNode->getPlayerId() << "registering player" << remotePlayerId;
    GameSender* gameSender = getGameSender(remotePlayerId);
    GameReceiver* gameReceiver = getGameReceiver(remotePlayerId, gameSender);
}