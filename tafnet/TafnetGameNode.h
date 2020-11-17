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

        std::function<GameSender * ()> m_gameSenderFactory;
        std::function<GameReceiver * (GameSender*)> m_gameReceiverFactory;

        GameSender* getGameSender(std::uint32_t remoteTafnetId);
        GameReceiver* getGameReceiver(std::uint32_t remoteTafnetId, GameSender* sender);
        virtual void handleGameData(QAbstractSocket* receivingSocket, int channelCode, char* data, int len);
        virtual void handleTafnetMessage(const TafnetMessageHeader& tafheader, std::uint32_t peerPlayerId, char* data, int len);

    public:
        TafnetGameNode(TafnetNode* tafnetNode, std::function<GameSender * ()> gameSenderFactory, std::function<GameReceiver * (GameSender*)> gameReceiverFactory);
      
        virtual void registerRemotePlayer(std::uint32_t remotePlayerId);

    };

}
