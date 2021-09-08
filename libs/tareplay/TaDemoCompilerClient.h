#pragma once

#include <QtCore/qset.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qhostaddress.h>
#include "gpgnet/GpgNetSend.h"
#include "tapacket/TPacket.h"

namespace tareplay {

    class TaDemoCompilerClient: public QObject, public tapacket::TaPacketHandler
    {
    public:
        struct ConnectionError : public std::exception
        { };

        TaDemoCompilerClient(QString taDemoCompilerHostName, quint16 taDemoCompilerPort, quint32 tafGameId, QString playerPublicAddr);
        ~TaDemoCompilerClient();

        void setHostPlayerName(QString name);
        void setLocalPlayerName(QString name);

        void sendHello(quint32 gameId, quint32 dplayPlayerId, QString playerPublicAddr);
        void sendGameInfo(quint16 maxUnits, QString mapName);
        void sendGamePlayer(qint8 side, QString name, QByteArray statusMessage);
        void sendGamePlayerNumber(quint32 dplayId, quint8 number);
        void sendGamePlayerLoading(QSet<quint32> lockedInPlayers);

        void sendUnitData(QByteArray unitData);

        // TADR file format requires:
        // - unencrypted
        // - 1 byte compressed (0x04) or uncomprossed (0x03)
        // - 0 bytes no checksum (normally 2 bytes)
        // - 0 bytes no timestamp (normally 4 bytes)
        void sendMoves(QByteArray moves);

    private:

        virtual void onDplaySuperEnumPlayerReply(std::uint32_t dplayId, const std::string& playerName, tapacket::DPAddress* tcp, tapacket::DPAddress* udp);
        virtual void onDplayCreateOrForwardPlayer(std::uint16_t command, std::uint32_t dplayId, const std::string& name, tapacket::DPAddress* tcp, tapacket::DPAddress* udp);
        virtual void onDplayDeletePlayer(std::uint32_t dplayId);

        virtual void onTaPacket(
            std::uint32_t sourceDplayId, std::uint32_t otherDplayId, bool isLocalSource,
            const char* encrypted, int sizeEncrypted,
            const std::vector<tapacket::bytestring>& subpaks);

        QTcpSocket m_tcpSocket;
        QString m_taDemoCompilerHostName;
        quint16 m_taDemoCompilerPort;
        quint32 m_tafGameId;
        QString m_playerPublicAddr;
        QString m_localPlayerName;
        QString m_hostPlayerName;
        quint32 m_localPlayerDplayId;
        quint32 m_hostDplayId;
        QDataStream m_datastream;
        gpgnet::GpgNetSend m_protocol;
        qint64 m_ticks;
        QSet<quint32> m_dpConnectedPlayers;
    };

}