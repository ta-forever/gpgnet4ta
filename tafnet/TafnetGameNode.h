#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include "TafnetNode.h"
#include "GameReceiver.h"
#include "GameSender.h"

class QAbstractSocket;

namespace tafnet
{

    class TafnetNode;
    class GameSender;
    class GameReceiver;
    struct TafnetMessageHeader;

    class TafnetGameNode
    {
        TafnetNode* m_tafnetNode;
        std::map<std::uint32_t, std::shared_ptr<GameSender> > m_gameSenders;     // keyed by peer playerId
        std::map<std::uint32_t, std::shared_ptr<GameReceiver> > m_gameReceivers; // keyed by peer playerId
        std::map<std::uint16_t, std::uint32_t> m_remotePlayerIds;                // keyed by gameReceiver's receive socket port (both tcp and udp)

        // these only discovered once some data received from game
        std::uint16_t m_gameTcpPort;
        std::uint16_t m_gameUdpPort;

        std::function<GameSender * ()> m_gameSenderFactory;
        std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> m_gameReceiverFactory;

        virtual GameSender* getGameSender(std::uint32_t remoteTafnetId);
        virtual GameReceiver* getGameReceiver(std::uint32_t remoteTafnetId, QSharedPointer<QUdpSocket> udpSocket);
        virtual void handleGameData(QAbstractSocket* receivingSocket, int channelCode, char* data, int len);
        virtual void handleTafnetMessage(const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len);
        virtual void translateMessageFromRemoteGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[]);
        virtual void translateMessageFromLocalGame(char* data, int len, std::uint32_t replyAddress, const std::uint16_t replyPorts[]);
        virtual void updateGameSenderPorts(const char *data, int len);

    public:
        TafnetGameNode(TafnetNode* tafnetNode, std::function<GameSender * ()> gameSenderFactory, std::function<GameReceiver * (QSharedPointer<QUdpSocket>)> gameReceiverFactory);
      
        virtual void registerRemotePlayer(std::uint32_t remotePlayerId);

        // GpgNetRunner uses a different dplay instance to check for when the host is up.
        // Once thats done we need to reset the ports so future replies go to the actual game, not gpgnetrunner instance
        virtual void resetGameConnection();

    };

}
