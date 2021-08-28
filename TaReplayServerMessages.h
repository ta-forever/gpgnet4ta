#pragma once

#include <qbytearray.h>
#include <qstring.h>
#include <qvariant.h>
#include <qvector.h>

class TaReplayServerSubscribe
{
public:
    quint32 gameId;
    quint32 position;

    static const char * const ID;
    TaReplayServerSubscribe();
    TaReplayServerSubscribe(QVariantList command);
    void set(QVariantList command);
};

enum class TaReplayServerStatus
{
    CONNECTING = 0,
    OK = 1,
    GAME_NOT_FOUND = 2
};

class TaReplayServerData
{
public:

    TaReplayServerStatus status;
    QByteArray data;

    static const char* const ID;
    TaReplayServerData();
    TaReplayServerData(QVariantList command);
    void set(QVariantList command);
};
