#pragma once

#include <qbytearray.h>
#include <qstring.h>
#include <qvariant.h>
#include <qvector.h>

namespace tareplay {

    class HelloMessage
    {
    public:
        quint32 gameId;
        quint32 playerDpId;
        QString playerName;

        static const char* const ID;
        HelloMessage();
        HelloMessage(QVariantList command);
        void set(QVariantList command);
    };

    class GameInfoMessage
    {
    public:
        quint16 maxUnits;
        QString mapName;

        static const char* const ID;
        GameInfoMessage();
        GameInfoMessage(QVariantList command);
        void set(QVariantList command);
    };

    class GamePlayerMessage
    {
    public:
        qint8 side;
        QString name;
        QByteArray statusMessage;

        static const char* const ID;
        GamePlayerMessage();
        GamePlayerMessage(QVariantList command);
        void set(QVariantList command);
    };

    class GamePlayerNumber
    {
    public:
        quint32 dplayId;
        quint8 number;

        static const char* const ID;
        GamePlayerNumber();
        GamePlayerNumber(QVariantList command);
        void set(QVariantList command);
    };

    class GamePlayerLoading
    {
    public:
        QVector<quint32> lockedInPlayers;

        static const char* const ID;
        GamePlayerLoading();
        GamePlayerLoading(QVariantList command);
        void set(QVariantList command);
    };

    class GameUnitDataMessage
    {
    public:
        QByteArray unitData;

        static const char* const ID;
        GameUnitDataMessage();
        GameUnitDataMessage(QVariantList command);
        void set(QVariantList command);
    };

    class GameMoveMessage
    {
    public:
        QByteArray moves;

        static const char* const ID;
        GameMoveMessage();
        GameMoveMessage(QVariantList command);
        void set(QVariantList command);
    };

    class StopRecordingMessage
    {
    public:
        static const char* const ID;
        StopRecordingMessage();
        StopRecordingMessage(QVariantList command);
        void set(QVariantList command);
    };

    class ReconnectMessage
    {
    public:
        quint32 gameId;
        quint32 playerDpId;

        static const char* const ID;
        ReconnectMessage();
        ReconnectMessage(QVariantList command);
        void set(QVariantList command);
    };

    class DebugDumpRequestMessage
    {
    public:
        quint32 gameId;

        static const char* const ID;
        DebugDumpRequestMessage();
        DebugDumpRequestMessage(QVariantList command);
        void set(QVariantList command);
    };

}