#pragma once

#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

namespace gpgnet
{
    struct CreateLobbyCommand
    {
        int protocol;
        int localPort;
        QString _playerName;
        QString playerAlias;
        QString playerRealName;
        int playerId;
        int natTraversal;

        static const char* const ID;
        CreateLobbyCommand();
        CreateLobbyCommand(QVariantList command);
        void Set(QVariantList command);
    };

    struct HostGameCommand
    {
        QString mapName;

        static const char* const ID;
        HostGameCommand();
        HostGameCommand(QVariantList qvl);
        void Set(QVariantList command);
    };

    struct JoinGameCommand
    {
        QString remoteHost;
        QString _remotePlayerName;
        QString remotePlayerAlias;
        QString remotePlayerRealName;
        int remotePlayerId;

        static const char* const ID;
        JoinGameCommand();
        JoinGameCommand(QVariantList qvl);
        void Set(QVariantList command);
    };

    struct ConnectToPeerCommand
    {
        QString host;
        QString _playerName;
        QString playerAlias;
        QString playerRealName;
        int playerId;

        static const char* const ID;
        ConnectToPeerCommand();
        ConnectToPeerCommand(QVariantList qvl);
        void Set(QVariantList command);
    };

    struct DisconnectFromPeerCommand
    {
        int playerId;

        static const char* const ID;
        DisconnectFromPeerCommand();
        DisconnectFromPeerCommand(QVariantList qvl);
        void Set(QVariantList command);
    };

    struct PingMessage
    {
        static const char* const ID;
        PingMessage();
        PingMessage(QVariantList qvl);
        void Set(QVariantList command);
    };

}
