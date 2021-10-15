#pragma once

#include "tapacket/UnitDataRepo.h"

namespace tareplay {

    class TaDemoCompiler: public QObject
    {
    public:
        TaDemoCompiler(QString demoPathTemplate, QHostAddress addr, quint16 port, quint32 minDemoSize);
        ~TaDemoCompiler();

        void sendStopRecordingToAllInGame(quint32 gameId);

    private:

        struct UserContext
        {
            UserContext() { }
            UserContext(QTcpSocket* socket);
            QString ipAddr();

            quint32 gameId;
            quint32 playerDpId;
            QString playerName;
            QSharedPointer<QDataStream> dataStream;
            gpgnet::GpgNetParse gpgNetParser;
            QSharedPointer<gpgnet::GpgNetSend> gpgNetSerialiser;
            GamePlayerMessage gamePlayerInfo;
            int gamePlayerNumber;   // 1..10
        };

        static const quint32 GAME_EXPIRY_TICKS = 60;
        struct GameContext
        {
            GameContext();
            QString getUnitDataHash() const;

            quint32 gameId;
            GameInfoMessage header;
            QMap<quint32, QSharedPointer<UserContext> > players;    // keyed by Dplay ID
            QVector<quint32> playersLockedIn;                       // those that actually progressed to loading
            tapacket::UnitDataRepo unitData;

            std::shared_ptr<std::ostream> demoCompilation;
            QString tempFileName;
            QString finalFileName;
            int expiryCountdown;    // continuing messages from players keep this counter from expiring
        };

        void onNewConnection();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        void onReadyRead();
        void closeExpiredGames();
        void pingUsers();
        void timerEvent(QTimerEvent* event);

        std::shared_ptr<std::ostream> commitHeaders(const GameContext& game, QString filename);
        void commitMove(const GameContext &, int playerNumber, const GameMoveMessage &);

        QString m_demoPathTemplate;
        quint32 m_minDemoSize;
        QTcpServer m_tcpServer;
        QMap<QTcpSocket*, QSharedPointer<UserContext> > m_players;
        QMap<quint32, GameContext> m_games;
        quint32 m_timerCounter;
    };

}
