#pragma once

#include <QtCore/qdatastream.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qhostaddress.h>

#include "GpgNetSend.h"

namespace gpgnet
{
    class GpgNetClient : public QObject, public GpgNetSend
    {
        Q_OBJECT

        QTcpSocket m_socket;
        QDataStream m_datastream;
        QMap<QString, quint32> m_gpgnetPlayerIds;

    public:
        GpgNetClient(QString gpgnetHostAndPort);
        quint32 lookupPlayerId(QString playerName);

    signals:
        void createLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal);
        void hostGame(QString mapName);
        void joinGame(QString host, QString playerName, int playerId);
        void connectToPeer(QString host, QString playerName, int playerId);
        void disconnectFromPeer(int playerId);

    private:
        void onReadyRead();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
    };

}
