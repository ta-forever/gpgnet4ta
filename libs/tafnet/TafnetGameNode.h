#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <queue>

#include "TafnetNode.h"
#include "GameReceiver.h"
#include "GameSender.h"

class QAbstractSocket;

namespace tapacket
{
    class TAPacketParser;
}

namespace tafnet
{

    class TafnetNode;
    class GameSender;
    class GameReceiver;
    struct TafnetMessageHeader;

    class TafnetGameNode
    {
        TafnetNode* m_tafnetNode;
        tapacket::TAPacketParser* m_packetParser;
        std::map<std::uint32_t, std::shared_ptr<GameSender> > m_gameSenders;     // keyed by peer tafnet playerId
        std::map<std::uint32_t, std::shared_ptr<GameReceiver> > m_gameReceivers; // keyed by peer tafnet playerId
        std::map<std::uint16_t, std::uint32_t> m_remotePlayerIds;                // tafnet id keyed by gameReceiver's receive socket port (both tcp and udp)
        std::map<std::uint32_t, QByteArray> m_pendingEnumRequests;               // keyed by peer tafnet playerId
        std::queue<std::uint32_t> m_playerInviteOrder;                      // useful only by host instance. controls order that enum requests are passed to game
        QTimer m_pendingEnumRequestsTimer;

        // these only discovered once some data received from game
        std::uint16_t m_gameTcpPort;
        std::uint16_t m_gameUdpPort;
        QHostAddress m_gameAddress;     // NB: upnp external address doesn't work
        const std::set<std::uint16_t> m_initialOccupiedTcpPorts;
        const std::set<std::uint16_t> m_initialOccupiedUdpPorts;
        QByteArray m_firstTcpPacket;

        std::function<GameSender * ()> m_gameSenderFactory;
        std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> m_gameReceiverFactory;

    public:
        TafnetGameNode(
            TafnetNode* tafnetNode,
            tapacket::TAPacketParser *packetParser,
            std::function<GameSender * ()> gameSenderFactory,
            std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> gameReceiverFactory);
        ~TafnetGameNode();

        virtual void registerRemotePlayer(std::uint32_t remotePlayerId, std::uint16_t isHostEnumPort);
        virtual void unregisterRemotePlayer(std::uint32_t remotePlayerId);
        virtual void sendEnumRequest(QUuid gameGuid, std::uint32_t asPeerId);

        virtual void messageToLocalPlayer(std::uint32_t sourceDplayId, std::uint32_t tafnetid, bool isPrivate, const std::string& nick, const std::string& chat);

        // useful only by host instance. controls order that enum requests are passed to game
        virtual void setPlayerInviteOrder(const std::vector<std::uint32_t>& tafnetIds);
        // useful only by host instance, and only if his tdraw.dll supports the share memory interface
        virtual void setPlayerStartPositions(const std::vector<std::string>& orderedPlayerNames);

    private:

        virtual GameSender* getGameSender(std::uint32_t remoteTafnetId);
        virtual GameReceiver* getGameReceiver(std::uint32_t remoteTafnetId, QSharedPointer<QUdpSocket> udpSocket);
        virtual void killGameSender(std::uint32_t remoteTafnetId);
        virtual void killGameReceiver(std::uint32_t remoteTafnetId);

        virtual void handleGameData(QAbstractSocket* receivingSocket, int channelCode, char* data, int len);
        virtual void handleTafnetMessage(std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len);
        virtual void translateMessageFromRemoteGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[]);
        virtual void translateMessageFromLocalGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[]);
        virtual void updateGameSenderPortsFromDplayHeader(const char *data, int len);
        virtual void updateGameSenderPortsFromSpaPacket(quint16 tcp, quint16 udp);

        virtual void processPendingEnumRequests();

        static std::set<std::uint16_t> probeOccupiedTcpPorts(QHostAddress address, std::uint16_t begin, std::uint16_t end, int timeoutms);
        static std::set<std::uint16_t> probeOccupiedUdpPorts(QHostAddress address, std::uint16_t begin, std::uint16_t end, int timeoutms);

        void* m_startPositionsHandle;
        void* m_startPositionsMemMap;
    };
}
