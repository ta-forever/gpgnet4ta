#include "TafnetGameNode.h"
#include "TafnetGameNode.h"
#include "GameAddressTranslater.h"
#include "GetUpnpPortMap.h"
#include "tademo/HexDump.h"
#include "tademo/TAPacketParser.h"
#include "tademo/DPlayPacket.h"

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
    GameAddressTranslater tx(replyAddress, replyPorts, [this](TADemo::DPAddress& address, int index) {
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
    //if (data)
    //{
    //    TADemo::HexDump(data, len, std::cout);
    //}
    if (m_remotePlayerIds.count(receivingSocket->localPort()) == 0)
    {
        qWarning() << "[TafnetGameNode::handleGameData] playerId" << m_tafnetNode->getPlayerId() << "unable to find a peerid associated with game data received on port" << receivingSocket->localPort();
        return;
    }

    std::uint32_t destNodeId = m_remotePlayerIds[receivingSocket->localPort()];
    if (destNodeId == 0)
    {
        qWarning() << "[TafnetGameNode::handleGameData] playerId" << m_tafnetNode->getPlayerId() << "encountered null peerid associated with game data received on port" << receivingSocket->localPort();
        return;
    }

    if (data && (m_gameTcpPort == 0 || m_gameUdpPort == 0))
    {
        updateGameSenderPorts(data, len);
    }

    if (!data)
    {
        resetGameConnection();
    }
    else if (channelCode == GameReceiver::CHANNEL_UDP)
    {
        m_tafnetNode->forwardGameData(destNodeId, Payload::ACTION_UDP_DATA, data, len);
        if (m_packetParser)
        {
            m_packetParser->parseGameData(data, len);
        }
    }

    else if (channelCode == GameReceiver::CHANNEL_TCP)
    {
        // our local ports are meaningless to the remote peer
        // what is important is the playerId that translateMessageFromLocalGame substitutes into the address field
        static const quint16 dummyports[] = { 0xdead, 0xbeef };
        translateMessageFromLocalGame(data, len, 0, dummyports);
        m_tafnetNode->forwardGameData(destNodeId, Payload::ACTION_TCP_DATA, data, len);
        if (m_packetParser)
        {
            m_packetParser->parseGameData(data, len);
        }
    }

    else if (channelCode == GameReceiver::CHANNEL_ENUM)
    {
        qInfo() << "[TafnetGameNode::handleGameData] playerId" << m_tafnetNode->getPlayerId() << "forwarding enum session request to" << destNodeId;
        m_tafnetNode->forwardGameData(destNodeId, Payload::ACTION_ENUM, data, len);
    }
}


void TafnetGameNode::translateMessageFromRemoteGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[])
{
    GameAddressTranslater tx(replyAddress, replyPorts, [this, replyAddress, replyPorts](TADemo::DPAddress& address, int index) {
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


void TafnetGameNode::handleTafnetMessage(std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len)
{
    GameSender* gameSender = getGameSender(peerPlayerId);
    GameReceiver* gameReceiver = getGameReceiver(peerPlayerId, gameSender->getUdpSocket());
    quint16 replyPorts[2];
    gameReceiver->getListenPorts(replyPorts);

    //qInfo() << "[TafnetGameNode::handleTafnetMessage] me=" << m_tafnetNode->getPlayerId() << "from=" << peerPlayerId << "action=" << tafheader.action;
    switch (action)
    {
    case Payload::ACTION_HELLO:
        // no further action beyond creating a gameSender/Receiver required
        break;

    case Payload::ACTION_ENUM:
        GameAddressTranslater(gameReceiver->getBindAddress().toIPv4Address(), replyPorts)(data, len);
        gameSender->enumSessions(data, len);

    case Payload::ACTION_TCP_OPEN:
        gameSender->openTcpSocket(500);
        break;

    case Payload::ACTION_TCP_DATA:
        translateMessageFromRemoteGame(data, len, gameReceiver->getBindAddress().toIPv4Address(), replyPorts);
        gameSender->sendTcpData(data, len);
        if (m_packetParser) m_packetParser->parseGameData(data, len);
        break;

    case Payload::ACTION_TCP_CLOSE:
        gameSender->closeTcpSocket();
        break;

    case Payload::ACTION_UDP_DATA:
        gameSender->sendUdpData(data, len);
        if (m_packetParser) m_packetParser->parseGameData(data, len);
        break;

    default:
        qInfo() << "[TafnetGameNode::handleTafnetMessage] playerId" << m_tafnetNode->getPlayerId() << "ERROR unknown action!";
        break;
    };
}

TafnetGameNode::TafnetGameNode(
    TafnetNode* tafnetNode,
    TADemo::TAPacketParser* gameMonitor,
    std::function<GameSender * ()> gameSenderFactory,
    std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> gameReceiverFactory) :
    m_tafnetNode(tafnetNode),
    m_packetParser(gameMonitor),
    m_gameTcpPort(0),
    m_gameUdpPort(0),
    m_gameSenderFactory(gameSenderFactory),
    m_gameReceiverFactory(gameReceiverFactory)
{
    m_tafnetNode->setHandler([this](std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len) {
        this->handleTafnetMessage(action, peerPlayerId, data, len);
    });
}

void TafnetGameNode::registerRemotePlayer(std::uint32_t remotePlayerId, std::uint16_t isHostEnumPort)
{
    qInfo() << "[TafnetGameNode::registerRemotePlayer] playerId" << m_tafnetNode->getPlayerId() << "registering peer" << remotePlayerId;
    GameSender* gameSender = getGameSender(remotePlayerId);
    GameReceiver* gameReceiver = getGameReceiver(remotePlayerId, gameSender->getUdpSocket());

    if (isHostEnumPort > 0)
    {
        m_gameReceivers[remotePlayerId]->bindEnumerationPort(isHostEnumPort);
        m_remotePlayerIds[gameReceiver->getEnumListenPort()] = remotePlayerId;
    }
}

void TafnetGameNode::unregisterRemotePlayer(std::uint32_t remotePlayerId)
{
    qInfo() << "[TafnetGameNode::unregisterRemotePlayer] playerId" << m_tafnetNode->getPlayerId() << "unregistering peer" << remotePlayerId;
    killGameSender(remotePlayerId);
    killGameReceiver(remotePlayerId);
}

void TafnetGameNode::updateGameSenderPorts(const char* data, int len)
{
    TADemo::DPHeader* header = (TADemo::DPHeader*)data;
    if (header->looksOk())
    {
        m_gameTcpPort = header->address.port();
        m_gameUdpPort = header->address.port() + 50;

        if (m_gameTcpPort < 2300 || m_gameUdpPort >= 2400)
        {
            // game is trying to use upnp ...
            qInfo() << "[TafnetGameNode::updateGameSenderPorts] playerId" << m_tafnetNode->getPlayerId() << "Game is trying to use upnp.external tcp port is" << m_gameTcpPort;
            try
            {
                std::uint16_t internalPort = GetUpnpPortMap(m_gameTcpPort, "TCP");
                qInfo() << "[TafnetGameNode::updateGameSenderPorts] playerId" << m_tafnetNode->getPlayerId() << "IGD reports corresponding internal TCP port is" << internalPort;
                m_gameTcpPort = internalPort;
                m_gameUdpPort = internalPort + 50;
            }
            catch (std::runtime_error &e)
            {
                qWarning() << "[TafnetGameNode::updateGameSenderPorts] playerId" << m_tafnetNode->getPlayerId() << "unable to get upnp port mappings : " << e.what();
            }
        }

        if (m_gameTcpPort < 2300 || m_gameUdpPort >= 2400)
        {
            qWarning() << "[TafnetGameNode::updateGameSenderPorts] playerId" << m_tafnetNode->getPlayerId() << "guessing the game ports ...";
            m_gameTcpPort = 2300;
            m_gameUdpPort = 2350;
        }

        qInfo() << "[TafnetGameNode::updateGameSenderPorts] playerId" << m_tafnetNode->getPlayerId() << "game ports set to tcp : " << m_gameTcpPort << " udp : " << m_gameUdpPort;
        for (auto& pair : m_gameSenders)
        {
            pair.second->setTcpPort(m_gameTcpPort);
            pair.second->setUdpPort(m_gameUdpPort);
        }
    }
    else
    {
        qWarning() << "[TafnetGameNode::updateGameSenderPorts] playerId" << m_tafnetNode->getPlayerId() << "cannot set game ports due to unrecognised dplay msg header";
        std::ostringstream ss;
        ss << '\n';
        TADemo::HexDump(data, len, ss);
        qWarning() << ss.str().c_str();
    }
}

void TafnetGameNode::resetGameConnection()
{
    qInfo() << "[TafnetGameNode::resetGameConnection] playerId" << m_tafnetNode->getPlayerId();
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

void TafnetGameNode::messageToLocalPlayer(std::uint32_t sourceDplayId, std::uint32_t tafnetid, const std::string& nick, const std::string& chat)
{
    //if (m_gameSenders.count(tafnetid) > 0)
    //{
    //    GameSender* sender = m_gameSenders[tafnetid].get();

    //    std::string message = "<" + nick + "> " + chat;

    //    TADemo::bytestring bs = TADemo::TPacket::createChatSubpacket(message);
    //    fs << "chat:\n";
    //    TADemo::HexDump(bs.data(), bs.size(), fs);

    //    bs = TADemo::TPacket::trivialSmartpak(bs, -1);
    //    fs << "paked chat\n";
    //    TADemo::HexDump(bs.data(), bs.size(), fs);

    //    bs = TADemo::TPacket::compress(bs);
    //    fs << "compressed chat\n";
    //    TADemo::HexDump(bs.data(), bs.size(), fs);

    //    bs = TADemo::TPacket::encrypt(bs);
    //    fs << "encrypted chat:\n";
    //    TADemo::HexDump(bs.data(), bs.size(), fs);

    //    //TADemo::bytestring bs = {
    //    //    0x04, 0xe4, 0x0f, 0x07, 0xed, 0xfa, 0x26, 0x07, 0x0d, 0x35, 0x4b, 0x73, 0x60, 0x0d, 0x6b, 0x3e,
    //    //    0x29, 0x26, 0x27, 0x2d, 0x34, 0x77, 0x1e, 0x76, 0x6c, 0x6d, 0x8a, 0x1b, 0x6e, 0x72, 0x71, 0x72,
    //    //    0xe8, 0x01, 0x41, 0x4b, 0x04, 0x24, 0x06, 0x41, 0x58, 0x28, 0x8a, 0x2a, 0x16, 0x4c, 0xab, 0x2f,
    //    //    0x30, 0xde, 0x30, 0xc7, 0x37, 0x00, 0x00, 0x6f
    //    //};

    //    qInfo() << "[TafnetGameNode::messageToLocalPlayer] sendTcpData encrypted chat";
    //    fs << "[TafnetGameNode::messageToLocalPlayer] " << sourceDplayId << ','<< tafnetid << ','<< nick.c_str() << ','<< chat.c_str() << '\n';
    //    TADemo::HexDump(bs.data(), bs.size(), fs);
    //    sender->sendTcpData((char*)bs.data(), bs.size());
    //}
}
