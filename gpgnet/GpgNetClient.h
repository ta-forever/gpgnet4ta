#pragma once

#include <QtCore/qdatastream.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qhostaddress.h>

#include "GpgNetSend.h"
#include "GpgNetParse.h"

namespace gpgnet
{
    class GpgNetClient : public QObject, public GpgNetSend
    {
        Q_OBJECT

        QTcpSocket m_socket;
        QDataStream m_datastream;
        GpgNetParse m_gpgNetParser;
        QMap<QString, quint32> m_gpgnetPlayerIds;

    public:
        GpgNetClient(QString gpgnetHostAndPort);
        quint32 lookupPlayerId(QString playerName);

        void sendGameState(QString state, QString substate);
        void sendCreateLobby(int /* eg 0 */, int /* eg 0xb254 */, const char* playerName, int /* eg 0x9195 */, int /* eg 1 */);
        void sendHostGame(QString mapName);
        void sendJoinGame(QString hostAndPort, QString remotePlayerName, int remotePlayerId);
        void sendGameMods(int numMods);
        void sendGameMods(QStringList uids);
        void sendGameOption(QString key, QString value);
        void sendGameOption(QString key, int value);
        void sendPlayerOption(QString playerId, QString key, QString value);
        void sendPlayerOption(QString playerId, QString key, int value);
        void sendAiOption(QString name, QString key, int value);
        void sendClearSlot(int slot);
        void sendGameEnded();
        void sendGameResult(int army, int score);

    signals:
        void createLobby(int protocol, int localPort, QString playerAlias, QString realName, int playerId, int natTraversal);
        void hostGame(QString mapName);
        void joinGame(QString host, QString playerAlias, QString realName, int playerId);
        void connectToPeer(QString host, QString playerAlias, QString realName, int playerId);
        void disconnectFromPeer(int playerId);

    private:
        void onReadyRead();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);

    };

}
