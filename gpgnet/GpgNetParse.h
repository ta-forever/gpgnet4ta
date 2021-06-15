#pragma once

#include <cstdint>

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

        CreateLobbyCommand();
        CreateLobbyCommand(QVariantList command);
        void Set(QVariantList command);
    };

    struct HostGameCommand
    {
        QString mapName;

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

        ConnectToPeerCommand();
        ConnectToPeerCommand(QVariantList qvl);
        void Set(QVariantList command);
    };

    struct DisconnectFromPeerCommand
    {
        int playerId;

        DisconnectFromPeerCommand();
        DisconnectFromPeerCommand(QVariantList qvl);
        void Set(QVariantList command);
    };

    class GpgNetParse
    {
        QDataStream& m_is;

        static std::uint8_t GetByte(QDataStream& is);
        static std::uint32_t GetInt(QDataStream& is);
        static QString GetString(QDataStream& is);

    public:
        GpgNetParse(QDataStream& is);
        QVariantList GetCommand();
        static QVariantList GetCommand(QDataStream& is);
    };

}
