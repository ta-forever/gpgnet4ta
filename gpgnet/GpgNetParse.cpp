#include "GpgNetParse.h"

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
        _remoteHost("127.0.0.1"),
        _remotePlayerName("ACDC"),
        remotePlayerId(0)
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
        _remoteHost = command[1].toString();
        _remotePlayerName = command[2].toString();
        remotePlayerId = command[3].toInt();
    }

    QString JoinGameCommand::remotePlayerName() const
    {
        return _remotePlayerName.split("@")[0];
    }

    QString JoinGameCommand::remoteHost() const
    {
        return _remoteHost;
    }

    QStringList JoinGameCommand::remoteHostCandidateList() const
    {
        QStringList candidates = _remotePlayerName.split("@");
        if (candidates.size() == 1)
        {
            candidates[0] = remoteHost();
        }
        else if (candidates.size() > 1)
        {
            candidates = candidates[1].split(';');
        }
        return candidates;
    }
        

    ConnectToPeerCommand::ConnectToPeerCommand() :
        _host("127.0.0.1"),
        _playerName("ACDC"),
        playerId(0)
    { }

    ConnectToPeerCommand::ConnectToPeerCommand(QVariantList qvl)
    {
        Set(qvl);
    }

    void ConnectToPeerCommand::Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("ConnectToPeer"))
        {
            throw std::runtime_error("Unexpected command");
        }
        _host = command[1].toString();
        _playerName = command[2].toString();
        playerId = command[3].toInt();
    }

    // _remotePlayerName might be in the format player@address1;address2. remotePlayerName() makes sure we get just a player name
    QString ConnectToPeerCommand::playerName() const
    {
        return _playerName.split("@")[0];
    }

    // the remote host actually in _remoteHost field
    QString ConnectToPeerCommand::host() const
    {
        return _host;
    }

    // the host list after the @ in _remotePlayerName if there is one.  otherwise just _remoteHost
    QStringList ConnectToPeerCommand::hostCandidateList() const
    {
        QStringList candidates = _playerName.split("@");
        if (candidates.size() == 1)
        {
            candidates[0] = host();
        }
        else if (candidates.size() > 1)
        {
            candidates = candidates[1].split(';');
        }
        return candidates;
    }

    DisconnectFromPeerCommand::DisconnectFromPeerCommand():
        playerId(0)
    { }

    DisconnectFromPeerCommand::DisconnectFromPeerCommand(QVariantList qvl)
    {
        Set(qvl);
    }

    void DisconnectFromPeerCommand::Set(QVariantList command)
    {
        QString cmd = command[0].toString();
        if (cmd.compare("DisconnectFromPeer"))
        {
            throw std::runtime_error("Unexpected command");
        }
        playerId = command[1].toInt();
    }

    std::uint8_t GpgNetParse::GetByte(QDataStream& is)
    {
        std::uint8_t byte;
        is.readRawData((char*)&byte, 1);
        return byte;
    }

    std::uint32_t GpgNetParse::GetInt(QDataStream& is)
    {
        std::uint32_t word;
        is.readRawData((char*)&word, 4);
        return word;
    }

    QString GpgNetParse::GetString(QDataStream& is)
    {
        std::uint32_t size = GetInt(is);
        QSharedPointer<char> buffer(new char[1 + size]);
        is.readRawData(buffer.data(), size);
        buffer.data()[size] = 0;
        return QString(buffer.data());
    }

    GpgNetParse::GpgNetParse(QDataStream& is) :
        m_is(is)
    { }

    QVariantList GpgNetParse::GetCommand()
    {
        return GetCommand(m_is);
    }

    QVariantList GpgNetParse::GetCommand(QDataStream& is)
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
