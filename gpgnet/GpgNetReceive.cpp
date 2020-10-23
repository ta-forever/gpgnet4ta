#include "GpgNetReceive.h"

#include <QtCore/qdatastream.h>
#include <QtCore/qsharedpointer.h>

namespace gpgnet
{

    CreateLobbyCommand::CreateLobbyCommand() :
        protocol(0),
        localPort(47625),
        playerName("BILLYIDOL"),
        playerId(1955),
        natTraversal(1)
    { }

    CreateLobbyCommand::CreateLobbyCommand(QVariantList command)
    {
        Set(command);
    }

    void CreateLobbyCommand::Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("CreateLobby"))
        {
            throw std::runtime_error("Unexpected command");
        }
        protocol = command[1].toInt();
        localPort = command[2].toInt();
        playerName = command[3].toString();
        playerId = command[4].toInt();
        natTraversal = command[5].toInt();
    }

    HostGameCommand::HostGameCommand() :
        mapName("SHERWOOD")
    { }

    HostGameCommand::HostGameCommand(QVariantList qvl)
    {
        Set(qvl);
    }

    void HostGameCommand::Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("HostGame"))
        {
            throw std::runtime_error("Unexpected command");
        }
        mapName = command[1].toString();
    }

    JoinGameCommand::JoinGameCommand() :
        hostAndPort("127.0.0.1:47625"),
        remotePlayerName("ACDC"),
        remotePlayerId(1973)
    { }

    JoinGameCommand::JoinGameCommand(QVariantList qvl)
    {
        Set(qvl);
    }

    void JoinGameCommand::Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("JoinGame"))
        {
            throw std::runtime_error("Unexpected command");
        }
        hostAndPort = command[1].toString();
        remotePlayerName = command[2].toString();
        remotePlayerId = command[3].toInt();

        QStringList playerParts = remotePlayerName.split("@");
        if (playerParts.size() == 2)
        {
            remotePlayerName = playerParts[0];
            hostAndPort = playerParts[1];
        }
    }

    std::uint8_t GpgNetReceive::GetByte(QDataStream& is)
    {
        std::uint8_t byte;
        is.readRawData((char*)&byte, 1);
        return byte;
    }

    std::uint32_t GpgNetReceive::GetInt(QDataStream& is)
    {
        std::uint32_t word;
        is.readRawData((char*)&word, 4);
        return word;
    }

    QString GpgNetReceive::GetString(QDataStream& is)
    {
        std::uint32_t size = GetInt(is);
        QSharedPointer<char> buffer(new char[1 + size]);
        is.readRawData(buffer.data(), size);
        buffer.data()[size] = 0;
        return QString(buffer.data());
    }

    GpgNetReceive::GpgNetReceive(QDataStream& is) :
        m_is(is)
    { }

    QVariantList GpgNetReceive::GetCommand()
    {
        return GetCommand(m_is);
    }

    QVariantList GpgNetReceive::GetCommand(QDataStream& is)
    {
        QVariantList commandAndArgs;

        QString command = GetString(is);
        std::uint32_t numArgs = GetInt(is);
        commandAndArgs.append(command);

        for (unsigned n = 0; n < numArgs; ++n)
        {
            std::uint8_t argType = GetByte(is);
            if (argType == 0)
            {
                std::uint32_t arg = GetInt(is);
                commandAndArgs.append(arg);
            }
            else if (argType == 1)
            {
                QString arg = GetString(is);
                commandAndArgs.append(arg);
            }
            else
            {
                throw std::runtime_error("unexpected argument type");
            }
        }
        return commandAndArgs;
    }
}
