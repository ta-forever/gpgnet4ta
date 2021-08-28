#include "GpgNetServerMessages.h"

namespace gpgnet
{

    static void SplitAliasAndRealName(QString aliasAndReal, QString& alias, QString& real)
    {
        QStringList parts = aliasAndReal.split("/");
        if (parts.size() > 1)
        {
            alias = parts[0];
            real = parts[1];
        }
        else
        {
            alias = parts[0];
            real = "";
        }
    }

    CreateLobbyCommand::CreateLobbyCommand() :
        protocol(0),
        localPort(47625),
        _playerName("BILLYIDOL"),
        playerAlias("BILLYIDOL"),
        playerRealName("BILLYIDOL"),
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
        _playerName = command[3].toString();
        SplitAliasAndRealName(_playerName, playerAlias, playerRealName);
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
        remoteHost("127.0.0.1"),
        _remotePlayerName("ACDC"),
        remotePlayerAlias("ACDC"),
        remotePlayerRealName("ACDC"),
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
        remoteHost = command[1].toString();
        _remotePlayerName = command[2].toString();
        remotePlayerId = command[3].toInt();
        SplitAliasAndRealName(_remotePlayerName, remotePlayerAlias, remotePlayerRealName);
    }

    ConnectToPeerCommand::ConnectToPeerCommand() :
        host("127.0.0.1"),
        _playerName("ACDC"),
        playerAlias("ACDC"),
        playerRealName("ACDC"),
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
        host = command[1].toString();
        _playerName = command[2].toString();
        playerId = command[3].toInt();
        SplitAliasAndRealName(_playerName, playerAlias, playerRealName);
    }

    DisconnectFromPeerCommand::DisconnectFromPeerCommand() :
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
}
