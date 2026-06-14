#pragma once

#include <qbytearray.h>
#include <qstring.h>
#include <qvariant.h>
#include <qvector.h>

namespace tareplay {

    class TaReplayServerSubscribe
    {
    public:
        quint32 gameId;
        quint32 position;
        // Optional signed watch ticket issued by the lobby (base64). Empty for
        // legacy clients that predate authenticated watch sessions.
        QByteArray ticket;

        static const char * const ID;
        TaReplayServerSubscribe();
        TaReplayServerSubscribe(QVariantList command);
        void set(QVariantList command);
    };

    enum class TaReplayServerStatus
    {
        CONNECTING = 0,
        OK = 1,
        GAME_NOT_FOUND = 2,
        LIVE_REPLAY_DISABLED = 3,
        AUTH_REQUIRED = 4
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

}

Q_DECLARE_METATYPE(tareplay::TaReplayServerStatus)
