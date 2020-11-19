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
        QString playerName;
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
        QString _remoteHost;
        QString _remotePlayerName;
        int remotePlayerId;

        JoinGameCommand();
        JoinGameCommand(QVariantList qvl);
        void Set(QVariantList command);

        // _remotePlayerName might be in the format player@address1;address2. remotePlayerName() makes sure we get just a player name
        QString remotePlayerName() const;

        // the remote host actually in _remoteHost field
        QString remoteHost() const;

        // the host list after the @ in _remotePlayerName if there is one.  otherwise just _remoteHost
        QStringList remoteHostCandidateList() const;
    };

    struct ConnectToPeerCommand
    {
        QString _host;
        QString _playerName;
        int playerId;

        ConnectToPeerCommand();
        ConnectToPeerCommand(QVariantList qvl);
        void Set(QVariantList command);

        // _remotePlayerName might be in the format player@address1;address2. remotePlayerName() makes sure we get just a player name
        QString playerName() const;

        // the remote host actually in _remoteHost field
        QString host() const;

        // the host list after the @ in _remotePlayerName if there is one.  otherwise just _remoteHost
        QStringList hostCandidateList() const;
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
