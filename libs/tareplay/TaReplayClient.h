#pragma once

#include "TaReplayServerMessages.h"

#include "QtCore/qdatastream.h"
#include "QtNetwork/qhostaddress.h"
#include "QtNetwork/qtcpsocket.h"

#include "gpgnet/GpgNetSend.h"
#include "gpgnet/GpgNetParse.h"

#include <fstream>

namespace tareplay {

    class TaReplayClient: public QObject
    {
    public:

        TaReplayClient(QHostAddress replayServerAddress, quint16 replayServerPort, quint32 tafGameId, quint32 position);
        ~TaReplayClient();

        TaReplayServerStatus getStatus() const;
        std::istream* getReplayStream();

    private:
        void timerEvent(QTimerEvent* event);
        void connect();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        void sendSubscribe(quint32 gameId, quint32 position);
        void onReadyRead();

        QHostAddress m_replayServerAddress;
        quint16 m_replayServerPort;
        quint16 m_tafGameId;
        quint32 m_position;

        QTcpSocket m_tcpSocket;
        QDataStream m_socketStream;
        gpgnet::GpgNetSend m_gpgNetSerialiser;
        gpgnet::GpgNetParse m_gpgNetParser;
        std::ofstream m_replayBufferOStream;
        std::ifstream m_replayBufferIStream;
        TaReplayServerStatus m_status;
        QString m_tempFilename;
    };

}