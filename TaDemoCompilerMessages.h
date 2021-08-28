#pragma once

#include <qbytearray.h>
#include <qstring.h>
#include <qvariant.h>
#include <qvector.h>

class HelloMessage
{
public:
    quint32 gameId;
    quint32 playerDpId;

    HelloMessage();
    HelloMessage(QVariantList command);
    void set(QVariantList command);
};

class GameInfoMessage
{
public:
    quint16 maxUnits;
    QString mapName;

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

    GamePlayerMessage();
    GamePlayerMessage(QVariantList command);
    void set(QVariantList command);
};

class GamePlayerNumber
{
public:
    quint32 dplayId;
    quint8 number;

    GamePlayerNumber();
    GamePlayerNumber(QVariantList command);
    void set(QVariantList command);
};

class GamePlayerLoading
{
public:
    QVector<quint32> lockedInPlayers;

    GamePlayerLoading();
    GamePlayerLoading(QVariantList command);
    void set(QVariantList command);
};

class GameUnitDataMessage
{
public:
    QByteArray unitData;

    GameUnitDataMessage();
    GameUnitDataMessage(QVariantList command);
    void set(QVariantList command);
};

class GameMoveMessage
{
public:
    QByteArray moves;

    GameMoveMessage();
    GameMoveMessage(QVariantList command);
    void set(QVariantList command);
};
