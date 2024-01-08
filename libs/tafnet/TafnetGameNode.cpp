#include "TafnetGameNode.h"
#include "GameAddressTranslater.h"
#include "taflib/Watchdog.h"
#include "taflib/HexDump.h"
#include "tapacket/TAPacketParser.h"
#include "tapacket/DPlayPacket.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <QtCore/quuid.h>

#ifdef WIN32
#include <windows.h>
#endif

using namespace tafnet;

static const std::uint16_t GAME_TCP_PORT_BEGIN = 2300;
static const std::uint16_t GAME_TCP_PORT_VALID_END = 2400;  // anything beyond this range will be assumed to indicate dplay is trying to use upnp
static const std::uint16_t GAME_TCP_PORT_PROBE_END = 2310;  // if dplay is trying to use upnp, we'll probe up to this port to find the local port being used by TA

static const std::uint16_t GAME_UDP_PORT_BEGIN = 2350;
static const std::uint16_t GAME_UDP_PORT_VALID_END = 2400;  // anything beyond this range will be assumed to indicate dplay is attempting to use upnp
static const std::uint16_t GAME_UDP_PORT_PROBE_END = 2360;  // if dplay is trying to use upnp, we'll probe up to this port to find the local port being used by TA

static const uint32_t TICKS_TO_PROTECT_UDP = 300u;  // 10 sec

GameSender* TafnetGameNode::getGameSender(std::uint32_t remoteTafnetId)
{
    std::shared_ptr<GameSender>& gameSender = m_gameSenders[remoteTafnetId];
    if (!gameSender)
    {
        gameSender.reset(m_gameSenderFactory());
        gameSender->setTcpPort(m_gameTcpPort);
        gameSender->setUdpPort(m_gameUdpPort);
        gameSender->setGameAddress(m_gameAddress);
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
    GameAddressTranslater tx(replyAddress, replyPorts, [this](tapacket::DPAddress& address, int index) {
        auto itPlayerId = m_remotePlayerIds.find(address.port());
        if (itPlayerId == m_remotePlayerIds.end())
        {
            // NB we haven't validated that this SPA genuinely belongs to local player. we only know that its not in our list of remotes ...
            // But if its actually an unlisted remote, there are bigger problems preventing this game from proceeding anyway
            qInfo() << "[TafnetGameNode::translateMessageFromLocalGame] (on local player SPA translate)"
                << "this.playerid:" << this->m_tafnetNode->getPlayerId()
                << "address:" << QHostAddress(address.address()).toString()
                << "index:" << index
                << "port:" << address.port();
            updateGameSenderPortsFromSpaPacket(index == 0 ? address.port() : 0, index == 1 ? address.port() : 0);
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
    taflib::Watchdog wd("TafnetGameNode::handleGameData", 100);

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

    if (!data)
    {
        qWarning() << "[TafnetGameNode::handleGameData] playerId" << m_tafnetNode->getPlayerId() << "encountered null data() on port" << receivingSocket->localPort();
        return;
    }

    if (len == 0)
    {
        static bool logged = false;
        if (!logged) {
            qInfo() << "[TafnetGameNode::handleGameData] playerId" << m_tafnetNode->getPlayerId() << "encountered empty data() on port. (this is expected when GameSender is spamming UDP)" << receivingSocket->localPort();
            logged = true;
        }
        return;
    }

    if (m_gameTcpPort == 0)
    {
        updateGameSenderPortsFromDplayHeader(data, len);
    }

    if (channelCode == GameReceiver::CHANNEL_UDP)
    {
        bool protect = unsigned(len) > m_tafnetNode->maxPacketSizeForPlayerId(destNodeId);
        protect |= m_packetParser && m_packetParser->getProgressTicks() < TICKS_TO_PROTECT_UDP;

        if (m_packetParser)
        {
            static const std::set<tapacket::SubPacketCode> protectedSubpaks({
                tapacket::SubPacketCode::CHAT_05,
                tapacket::SubPacketCode::LOADING_STARTED_08,
                tapacket::SubPacketCode::GIVE_UNIT_14,
                tapacket::SubPacketCode::HOST_MIGRATION_18,
                tapacket::SubPacketCode::REJECT_1B,
                tapacket::SubPacketCode::SPEED_19,
                tapacket::SubPacketCode::ALLY_23,
                tapacket::SubPacketCode::TEAM_24
            });

            std::set<tapacket::SubPacketCode> parsedSubpaks = m_packetParser->parseGameData(true, data, len);
            std::vector<tapacket::SubPacketCode> parsedProtectedSubpaks;
            std::set_intersection(
                parsedSubpaks.begin(), parsedSubpaks.end(),
                protectedSubpaks.begin(), protectedSubpaks.end(),
                std::back_inserter(parsedProtectedSubpaks));
            protect |= parsedProtectedSubpaks.size() > 0u;
        }

        if (protect)
        {
            // split/reassemble with ack/resend to ensure delivery to remote TafnetNode, but still delivered to game's UDP port
            m_tafnetNode->forwardGameData(destNodeId, Payload::ACTION_UDP_PROTECTED, data, len);
        }
        else
        {
            m_tafnetNode->forwardGameData(destNodeId, Payload::ACTION_UDP_DATA, data, len);
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
            m_packetParser->parseGameData(true, data, len);
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
    GameAddressTranslater tx(replyAddress, replyPorts, [this, replyAddress, replyPorts](tapacket::DPAddress& address, int index) {
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
    taflib::Watchdog wd("TafnetGameNode::handleTafnetMessage", 100);
    GameSender* gameSender = getGameSender(peerPlayerId);
    GameReceiver* gameReceiver = getGameReceiver(peerPlayerId, gameSender->getUdpSocket());
    quint16 replyPorts[2];
    gameReceiver->getListenPorts(replyPorts);

    switch (action)
    {
    case Payload::ACTION_HELLO:
        m_tafnetNode->sendPacksizeTests(peerPlayerId);
        break;

    case Payload::ACTION_ENUM:
        GameAddressTranslater(gameReceiver->getBindAddress().toIPv4Address(), replyPorts)(data, len);
        m_pendingEnumRequests[peerPlayerId] = QByteArray(data, len);
        break;

    case Payload::ACTION_TCP_OPEN:
        gameSender->openTcpSocket(500);
        break;

    case Payload::ACTION_TCP_DATA:
        translateMessageFromRemoteGame(data, len, gameReceiver->getBindAddress().toIPv4Address(), replyPorts);
        gameSender->sendTcpData(data, len);
        if (m_packetParser)
        {
            m_packetParser->parseGameData(false, data, len);
        }
        break;

    case Payload::ACTION_UDP_PROTECTED:
    case Payload::ACTION_UDP_DATA:
        gameSender->sendUdpData(data, len);
        if (m_packetParser)
        {
            m_packetParser->parseGameData(false, data, len);
        }
        break;

    default:
        qInfo() << "[TafnetGameNode::handleTafnetMessage] playerId" << m_tafnetNode->getPlayerId() << "ERROR unknown action!";
        break;
    };
}

template<typename IteratorT>
static QStringList numbersToQString(IteratorT begin, IteratorT end)
{
    QStringList stringList;
    std::for_each(begin, end, [&stringList](typename IteratorT::value_type n) {
        stringList.append(QString::number(n));
    });
    return stringList;
}

TafnetGameNode::TafnetGameNode(
    TafnetNode* tafnetNode,
    tapacket::TAPacketParser* gameMonitor,
    std::function<GameSender * ()> gameSenderFactory,
    std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> gameReceiverFactory) :
    m_startPositionsHandle(NULL),
    m_startPositionsMemMap(NULL),
    m_tafnetNode(tafnetNode),
    m_packetParser(gameMonitor),
    m_gameTcpPort(0),
    m_gameUdpPort(0),
    m_gameAddress(QHostAddress::SpecialAddress::LocalHost),
    m_gameSenderFactory(gameSenderFactory),
    m_gameReceiverFactory(gameReceiverFactory),
    m_initialOccupiedTcpPorts(probeOccupiedTcpPorts(QHostAddress(QHostAddress::SpecialAddress::LocalHost), GAME_TCP_PORT_BEGIN, GAME_TCP_PORT_PROBE_END, 30)),
    m_initialOccupiedUdpPorts(probeOccupiedUdpPorts(QHostAddress(QHostAddress::SpecialAddress::LocalHost), GAME_UDP_PORT_BEGIN, GAME_UDP_PORT_PROBE_END, 30))
{
    qInfo() << "[TafnetGameNode::TafnetGameNode] occupied TCP ports on localhost at time of construction:" 
        << numbersToQString(m_initialOccupiedTcpPorts.begin(), m_initialOccupiedTcpPorts.end()).join(",");

    qInfo() << "[TafnetGameNode::TafnetGameNode] occupied UDP ports on localhost at time of construction:"
        << numbersToQString(m_initialOccupiedUdpPorts.begin(), m_initialOccupiedUdpPorts.end()).join(",");

    m_tafnetNode->setHandler([this](std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len) {
        this->handleTafnetMessage(action, peerPlayerId, data, len);
    });

    m_pendingEnumRequestsTimer.connect(&m_pendingEnumRequestsTimer, &QTimer::timeout, [this] { processPendingEnumRequests(); });
    m_pendingEnumRequestsTimer.setInterval(1000);
    m_pendingEnumRequestsTimer.start();
}

TafnetGameNode::~TafnetGameNode()
{
#ifdef WIN32
    if (m_startPositionsHandle != NULL)
    {
        UnmapViewOfFile(m_startPositionsHandle);
        CloseHandle(m_startPositionsHandle);
    }
    m_startPositionsHandle = m_startPositionsMemMap = NULL;
#endif
}

std::set<std::uint16_t> TafnetGameNode::probeOccupiedTcpPorts(QHostAddress address, std::uint16_t begin, std::uint16_t end, int timeoutms)
{
    std::set<std::uint16_t> occupiedPorts;
    for (quint16 port = begin; port < end; ++port) {
        QTcpSocket probe;
        probe.connectToHost(address, port);
        if (probe.waitForConnected(timeoutms)) {
            occupiedPorts.insert(port);
        }
    }
    return occupiedPorts;
}

std::set<std::uint16_t> TafnetGameNode::probeOccupiedUdpPorts(QHostAddress address, std::uint16_t begin, std::uint16_t end, int timeoutms)
{
    std::set<std::uint16_t> occupiedPorts;
    for (quint16 port = begin; port < end; ++port) {
        QUdpSocket probe;
        // NB this isn't diagnostic as its possible to bind UDP port in shared mode.  Infact this is what TA does if not launched as admin ...
        if (!probe.bind(address, port))
        {
            occupiedPorts.insert(port);
        }
    }
    return occupiedPorts;
}

void TafnetGameNode::sendEnumRequest(QUuid gameGuid, std::uint32_t asPeerId)
{
    //GameSender* sender = getGameSender(asPeerId);
    //GameReceiver* receiver = getGameReceiver(asPeerId, sender->getUdpSocket());

    //GUID guid(gameGuid);
    //TADemo::DPEnumReq enumreq((std::uint8_t*)& guid);
    //TADemo::DPHeader dpheader(receiver->getBindAddress().toIPv4Address(), receiver->getTcpListenPort(),
    //    "play", TADemo::DPlayCommandCode::ENUMSESSIONS, 0x000e, sizeof(enumreq));
    //TADemo::bytestring bs = TADemo::bytestring((const std::uint8_t*) & dpheader, sizeof(dpheader)) + TADemo::bytestring((const std::uint8_t*) & enumreq, sizeof(enumreq));
    //TADemo::HexDump(bs.data(), bs.size(), fs);
    //sender->enumSessions((char*)bs.data(), bs.size());
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
    m_pendingEnumRequests.erase(remotePlayerId);

    std::queue<std::uint32_t> remainingRemotePlayersJoinOrder;
    while (!m_playerInviteOrder.empty())
    {
        if (m_playerInviteOrder.front() != remotePlayerId)
        {
            remainingRemotePlayersJoinOrder.push(m_playerInviteOrder.front());
        }
        m_playerInviteOrder.pop();
    }
    m_playerInviteOrder = remainingRemotePlayersJoinOrder;

    killGameSender(remotePlayerId);
    killGameReceiver(remotePlayerId);
}

void TafnetGameNode::updateGameSenderPortsFromDplayHeader(const char* data, int len)
{
    taflib::Watchdog wd2("TafnetGameNode::updateGameSenderPortsFromDplayHeader", 400);
    tapacket::DPHeader* header = (tapacket::DPHeader*)data;
    if (header->looksOk())
    {
        m_gameTcpPort = header->address.port();
        m_gameAddress = QHostAddress(QHostAddress::SpecialAddress::LocalHost);

        std::set<std::uint16_t> occupiedTcpPorts = probeOccupiedTcpPorts(m_gameAddress, GAME_TCP_PORT_BEGIN, GAME_TCP_PORT_PROBE_END, 10);
        qInfo() << "[TafnetGameNode::updateGameSenderPortsFromDplayHeader] TCP port probe:" << m_gameAddress << numbersToQString(occupiedTcpPorts.begin(), occupiedTcpPorts.end()).join(",");

        // UDP port probe is for logging purposes only since its pretty hit and miss
        std::set<std::uint16_t> occupiedUdpPorts = probeOccupiedUdpPorts(m_gameAddress, GAME_UDP_PORT_BEGIN, GAME_UDP_PORT_PROBE_END, 10);
        qInfo() << "[TafnetGameNode::updateGameSenderPortsFromDplayHeader] UDP port probe:" << m_gameAddress << numbersToQString(occupiedUdpPorts.begin(), occupiedUdpPorts.end()).join(",");

        if (m_gameTcpPort < GAME_TCP_PORT_BEGIN || m_gameTcpPort >= GAME_TCP_PORT_VALID_END)
        {
            for (std::uint16_t port : occupiedTcpPorts) {
                if (m_initialOccupiedTcpPorts.count(port) == 0) {
                    qInfo() << "[TafnetGameNode::updateGameSenderPortsFromDplayHeader] TCP port probe:" << port << "seems likely belonging to game";
                    m_gameTcpPort = port;
                }
            }
        }

        if (m_gameTcpPort < GAME_TCP_PORT_BEGIN || m_gameTcpPort >= GAME_TCP_PORT_VALID_END) {
            qWarning() << "[TafnetGameNode::updateGameSenderPortsFromDplayHeader] playerId" << m_tafnetNode->getPlayerId() << "guessing game tcp port ...";
            m_gameTcpPort = GAME_TCP_PORT_BEGIN;
        }

        //m_gameUdpPort = m_gameTcpPort+50;

        qInfo() << "[TafnetGameNode::updateGameSenderPortsFromDplayHeader] playerId" << m_tafnetNode->getPlayerId() << "game address set to" << QHostAddress(m_gameAddress).toString() << m_gameTcpPort << "tcp" << m_gameUdpPort << "udp";
        for (auto& pair : m_gameSenders)
        {
            pair.second->setTcpPort(m_gameTcpPort);
            pair.second->setUdpPort(m_gameUdpPort);
            pair.second->setGameAddress(m_gameAddress);
        }
    }
    else
    {
        qWarning() << "[TafnetGameNode::updateGameSenderPortsFromDplayHeader] playerId" << m_tafnetNode->getPlayerId() << "cannot set game ports due to unrecognised dplay msg header";
        std::ostringstream ss;
        ss << '\n';
        taflib::HexDump(data, len, ss);
        qWarning() << ss.str().c_str();
    }
}

void TafnetGameNode::updateGameSenderPortsFromSpaPacket(quint16 tcp, quint16 udp)
{
    if (tcp >= GAME_TCP_PORT_BEGIN && tcp < GAME_TCP_PORT_VALID_END && m_gameTcpPort == 0)
    {
        qInfo() << "[TafnetGameNode::updateGameSenderPortsFromSpaPacket] playerId" << m_tafnetNode->getPlayerId() << "game TCP port set to" << tcp;
        m_gameTcpPort = tcp;
        for (auto& pair : m_gameSenders)
        {
            pair.second->setTcpPort(tcp);
        }
    }

    if (udp >= GAME_UDP_PORT_BEGIN && udp < GAME_UDP_PORT_VALID_END && m_gameUdpPort == 0)
    {
        qInfo() << "[TafnetGameNode::updateGameSenderPortsFromSpaPacket] playerId" << m_tafnetNode->getPlayerId() << "game UDP port set to" << udp;
        m_gameUdpPort = udp;
        for (auto& pair : m_gameSenders)
        {
            pair.second->setUdpPort(udp);
        }
    }
}

void TafnetGameNode::messageToLocalPlayer(std::uint32_t sourceDplayId, std::uint32_t tafnetid, bool isPrivate, const std::string& nick, const std::string& chat)
{
    if (!m_gameSenders.empty())
    {
        GameSender* sender = m_gameSenders.begin()->second.get();
        std::string message(chat);
        if (nick.size() > 0)
        {
            if (isPrivate)
            {
                message = "!<" + nick + "> " + chat;
            }
            else
            {
                message = "*<" + nick + "> " + chat;
            }
        }

        tapacket::bytestring bs = tapacket::TPacket::createChatSubpacket(message);
        bs = tapacket::TPacket::trivialSmartpak(bs, -1);
        bs = tapacket::TPacket::compress(bs);
        tapacket::TPacket::encrypt(bs);

        std::uint32_t zeroDplayId = 0u;
        bs = tapacket::bytestring((std::uint8_t*) & sourceDplayId, 4)
            + tapacket::bytestring((std::uint8_t*) & zeroDplayId, 4)
            + bs;

        sender->sendUdpData((char*)bs.data(), bs.size());
    }
}

void TafnetGameNode::setPlayerInviteOrder(const std::vector<std::uint32_t>& tafnetIds)
{
    std::ostringstream ss;
    for (std::uint32_t id: tafnetIds)
    {
        ss << id << ' ';
    }
    qInfo() << "[TafnetGameNode::setPlayerInviteOrder] " << ss.str().c_str();

    m_playerInviteOrder = std::queue<std::uint32_t>();
    for (std::uint32_t id : tafnetIds)
    {
        if (m_gameSenders.find(id) == m_gameSenders.end())
        {
            qWarning() << "[TafnetGameNode::setPlayerInviteOrder] ignoring unknown pid:" << id;
        }
        else
        {
            m_playerInviteOrder.push(id);
        }
    }
}

struct StartPositionsShare
{
    std::uint32_t positionCount;
    char orderedPlayerNames[10][32];
};

void TafnetGameNode::setPlayerStartPositions(const std::vector<std::string>& orderedPlayerNames)
{
#ifdef WIN32
    if (m_startPositionsHandle == NULL)
    {
        m_startPositionsHandle = CreateFileMapping((HANDLE)0xFFFFFFFF,
            NULL,
            PAGE_READWRITE,
            0,
            sizeof(StartPositionsShare),
            "TADemo-StartPositions");

        bool bExists = (GetLastError() == ERROR_ALREADY_EXISTS);

        m_startPositionsMemMap = MapViewOfFile(m_startPositionsHandle,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(StartPositionsShare));
    }

    if (m_startPositionsHandle && m_startPositionsMemMap)
    {
        memset(m_startPositionsMemMap, 0, sizeof(StartPositionsShare));
        StartPositionsShare* sm = static_cast<StartPositionsShare*>(m_startPositionsMemMap);
        sm->positionCount = orderedPlayerNames.size();
        for (int n = 0; n < orderedPlayerNames.size(); ++n)
        {
            std::strncpy(sm->orderedPlayerNames[n], orderedPlayerNames[n].c_str(), sizeof(sm->orderedPlayerNames[n]));
            sm->orderedPlayerNames[n][sizeof(sm->orderedPlayerNames[n])-1] = '\0';
        }

        std::ostringstream ss;
        for (int n = 0; n < 10; ++n)
        {
            ss << sm->orderedPlayerNames[n] << ' ';
        }
        qInfo() << "[TafnetGameNode::setPlayerStartPositions] count=" << sm->positionCount << "names=" << ss.str().c_str();
    }
#else
    qWarning() << "[TafnetGameNode::setPlayerStartPositions] only supported on WIN32";
#endif
}

void TafnetGameNode::processPendingEnumRequests()
{
    taflib::Watchdog wd("[TafnetGameNode::processPendingEnumRequests]", 100);
    try
    {
        auto itToAdmit = m_pendingEnumRequests.end();
        if (!m_playerInviteOrder.empty())
        {
            itToAdmit = m_pendingEnumRequests.find(m_playerInviteOrder.front());
        }
        else
        {
            itToAdmit = m_pendingEnumRequests.begin();
        }

        if (itToAdmit != m_pendingEnumRequests.end())
        {
            std::uint32_t peerId = std::get<0>(*itToAdmit);
            const QByteArray& datas = std::get<1>(*itToAdmit);
            GameSender* gameSender = getGameSender(peerId);
            bool ok = gameSender->enumSessions(datas.constData(), datas.size());
            if (!ok)
            {
                qInfo() << "[TafnetGameNode::processPendingEnumRequests] enum port no connection ...";
            }
            else
            {
                qInfo() << "[TafnetGameNode::processPendingEnumRequests] admitting pid:" << peerId;
                if (!m_playerInviteOrder.empty())
                {
                    m_playerInviteOrder.pop();
                }
                m_pendingEnumRequests.erase(itToAdmit);
            }
            qInfo() << "[TafnetGameNode::processPendingEnumRequests]" << m_pendingEnumRequests.size() << "players knocking," << m_playerInviteOrder.size() << "invited";
        }

        if (m_packetParser && m_packetParser->getProgressTicks() > 0u)
        {
            qInfo() << "[TafnetGameNode::processPendingEnumRequests] game ticks > 0.  terminating enumTickTimer with" << m_pendingEnumRequests.size() << "enum requests in queue";
            m_pendingEnumRequestsTimer.stop();
        }
    }
    catch (std::exception & e)
    {
        qCritical() << "[TafnetGameNode::processPendingEnumRequests] unexpected exception:" << e.what();

    }
    catch (...)
    {
        qCritical() << "[TafnetGameNode::processPendingEnumRequests] unknown exception";
    }
}
