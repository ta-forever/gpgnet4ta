#pragma once

#include <QtCore/qdatastream.h>

namespace gpgnet
{

    class GpgNetSend
    {
        QDataStream& m_os;

        void SendCommand(QString command, int argumentCount);
        void SendArgument(QString arg);
        void SendArgument(int arg);

    public:

        GpgNetSend(QDataStream& os);

        void gameState(QString state);
        void createLobby(int /* eg 0 */, int /* eg 0xb254 */, const char* playerName, int /* eg 0x9195 */, int /* eg 1 */);
        void hostGame(QString mapName);
        void joinGame(QString hostAndPort, QString remotePlayerName, int remotePlayerId);
        void gameMods(int numMods);
        void gameMods(QStringList uids);
        void gameOption(QString key, QString value);
        void gameOption(QString key, int value);
        void playerOption(QString playerId, QString key, QString value);
        void playerOption(QString playerId, QString key, int value);
        void aiOption(QString key, QString value);
        void clearSlot(int slot);
        void gameEnded();
        void gameResult(int army, int score);

    };

}
