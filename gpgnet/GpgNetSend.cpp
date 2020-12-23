#include "GpgNetSend.h"
#include <QtCore/qDebug.h>

namespace gpgnet
{
    GpgNetSend::GpgNetSend(QDataStream& os) :
        m_os(os)
    { }

    void GpgNetSend::SendCommand(QString command, int argumentCount)
    {
        qInfo() << "GpgNetSend command" << command;
        m_os << command.toUtf8() << quint32(argumentCount);
    }

    void GpgNetSend::SendArgument(QString arg)
    {
        qInfo() << "GpgNetSend arg" << arg;
        m_os << quint8(1) << arg.toUtf8();
    }

    void GpgNetSend::SendArgument(int arg)
    {

        qInfo() << "GpgNetSend arg" << arg;
        m_os << quint8(0) << quint32(arg);
    }

    void GpgNetSend::gameState(QString state)
    {
        SendCommand("GameState", 1);
        SendArgument(state);
    }

    void GpgNetSend::createLobby(int /* eg 0 */, int /* eg 0xb254 */, const char* playerName, int /* eg 0x9195 */, int /* eg 1 */)
    {
        throw std::runtime_error("not implemented");
    }

    void GpgNetSend::hostGame(QString mapName)
    {
        SendCommand("HostGame", 1);
        SendArgument(mapName);
    }

    void GpgNetSend::joinGame(QString hostAndPort, QString remotePlayerName, int remotePlayerId)
    {
        SendCommand("JoinGame", 3);
        SendArgument(remotePlayerName);
        SendArgument(remotePlayerId);
    }

    void GpgNetSend::gameMods(int numMods)
    {
        SendCommand("GameMods", 2);
        SendArgument("activated");
        SendArgument(numMods);
    }

    void GpgNetSend::gameMods(QStringList uids)
    {
        SendCommand("GameMods", 2);
        SendArgument("uids");
        SendArgument(uids.join(' '));
    }

    void GpgNetSend::gameOption(QString key, QString value)
    {
        SendCommand("GameOption", 2);
        SendArgument(key);
        SendArgument(value);
    }

    void GpgNetSend::gameOption(QString key, int value)
    {
        SendCommand("GameOption", 2);
        SendArgument(key);
        SendArgument(value);
    }

    void GpgNetSend::playerOption(QString playerId, QString key, QString value)
    {
        SendCommand("PlayerOption", 3);
        SendArgument(playerId);
        SendArgument(key);
        SendArgument(value);
    }

    void GpgNetSend::playerOption(QString playerId, QString key, int value)
    {
        SendCommand("PlayerOption", 3);
        SendArgument(playerId);
        SendArgument(key);
        SendArgument(value);
    }

    void GpgNetSend::aiOption(QString name, QString key, int value)
    {
        SendCommand("AIOption", 3);
        SendArgument(name);
        SendArgument(key);
        SendArgument(value);
    }

    void GpgNetSend::clearSlot(int slot)
    {
        SendCommand("ClearSlot", 1);
        SendArgument(slot);
    }

    void GpgNetSend::gameEnded()
    {
        SendCommand("GameEnded", 0);
    }

    void GpgNetSend::gameResult(int army, int score)
    {
        SendCommand("GameResult", 2);
        SendArgument(army);
        if (score > 0)
        {
            SendArgument("VICTORY 1");
        }
        else if (score == 0)
        {
            SendArgument("DRAW 0");
        }
        else
        {
            SendArgument("DEFEAT -1");
        }
    }

};
