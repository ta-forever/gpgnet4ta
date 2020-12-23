#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include "TafnetNode.h"
#include "GameReceiver.h"
#include "GameSender.h"

class QAbstractSocket;

namespace TADemo
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
        TADemo::TAPacketParser* m_packetParser;
        std::map<std::uint32_t, std::shared_ptr<GameSender> > m_gameSenders;     // keyed by peer tafnet playerId
        std::map<std::uint32_t, std::shared_ptr<GameReceiver> > m_gameReceivers; // keyed by peer tafnet playerId
        std::map<std::uint16_t, std::uint32_t> m_remotePlayerIds;                // tafnet id keyed by gameReceiver's receive socket port (both tcp and udp)

        // these only discovered once some data received from game
        std::uint16_t m_gameTcpPort;
        std::uint16_t m_gameUdpPort;

        std::function<GameSender * ()> m_gameSenderFactory;
        std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> m_gameReceiverFactory;

    public:
        TafnetGameNode(
            TafnetNode* tafnetNode,
            TADemo::TAPacketParser *packetParser,
            std::function<GameSender * ()> gameSenderFactory,
            std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> gameReceiverFactory);

        virtual void registerRemotePlayer(std::uint32_t remotePlayerId, std::uint16_t isHostEnumPort);
        virtual void unregisterRemotePlayer(std::uint32_t remotePlayerId);
        virtual void resetGameConnection();
        virtual void sendEnumRequest(QUuid gameGuid, std::uint32_t asPeerId);

        virtual void messageToLocalPlayer(std::uint32_t sourceDplayId, std::uint32_t tafnetid, bool isPrivate, const std::string& nick, const std::string& chat);

    private:

        virtual GameSender* getGameSender(std::uint32_t remoteTafnetId);
        virtual GameReceiver* getGameReceiver(std::uint32_t remoteTafnetId, QSharedPointer<QUdpSocket> udpSocket);
        virtual void killGameSender(std::uint32_t remoteTafnetId);
        virtual void killGameReceiver(std::uint32_t remoteTafnetId);

        virtual void handleGameData(QAbstractSocket* receivingSocket, int channelCode, char* data, int len);
        virtual void handleTafnetMessage(std::uint8_t action, std::uint32_t peerPlayerId, char* data, int len);
        virtual void translateMessageFromRemoteGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[]);
        virtual void translateMessageFromLocalGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[]);
        virtual void updateGameSenderPorts(const char *data, int len);
    };
}
