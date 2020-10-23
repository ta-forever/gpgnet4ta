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
        QString hostAndPort;
        QString remotePlayerName;
        int remotePlayerId;

        JoinGameCommand();
        JoinGameCommand(QVariantList qvl);
        void Set(QVariantList command);
    };

    class GpgNetReceive
    {
        QDataStream& m_is;

        static std::uint8_t GetByte(QDataStream& is);
        static std::uint32_t GetInt(QDataStream& is);
        static QString GetString(QDataStream& is);

    public:
        GpgNetReceive(QDataStream& is);
        QVariantList GetCommand();
        static QVariantList GetCommand(QDataStream& is);
    };
}
