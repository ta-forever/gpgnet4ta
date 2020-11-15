#include "TafnetGameNode.h"

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

void TafnetGameNode::handleGameData(QAbstractSocket* receivingSocket, int channelCode, char* data, int len)
{
    qDebug() << "[TafnetGameNode::handleGameData] recievePort=" << receivingSocket->localPort() << "channelCode=" << channelCode << "len=" << len;
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
        m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_TCP_DATA, data, len);
    }
    else if (channelCode == GameReceiver::CHANNEL_ENUM)
    {
        m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_ENUM, data, len);
    }
}

void TafnetGameNode::handleTafnetMessage(const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len)
{
    GameSender* gameSender = getGameSender(peerPlayerId);
    GameReceiver* gameReceiver = getGameReceiver(peerPlayerId, gameSender);
    quint16 replyPorts[2];
    qDebug() << "[TafnetGameNode::handleTafnetMessage] me=" << m_tafnetNode->getPlayerId() << "from=" << peerPlayerId << "action=" << tafheader.action;
    switch (tafheader.action)
    {
    case TafnetMessageHeader::ACTION_HELLO:
        // no further action beyond creating a gameSender/Receiver required
        break;
    case TafnetMessageHeader::ACTION_ENUM:
        gameSender->enumSessions(data, len, gameReceiver->getBindAddress(), gameReceiver->getListenPorts(replyPorts));
        break;
    case TafnetMessageHeader::ACTION_TCP_OPEN:
        gameSender->openTcpSocket(3);
        break;
    case TafnetMessageHeader::ACTION_TCP_DATA:
        gameSender->sendTcpData(data, len, gameReceiver->getBindAddress(), gameReceiver->getListenPorts(replyPorts));
        break;
    case TafnetMessageHeader::ACTION_TCP_CLOSE:
        gameSender->closeTcpSocket();
        break;
    case TafnetMessageHeader::ACTION_UDP_DATA:
        gameSender->sendUdpData(data, len);
        break;
    default:
        qDebug() << "[TafnetGameNode::<tafmsg handleTafnetMessage>] ERROR unknown action!";
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
