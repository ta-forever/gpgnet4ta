#pragma once

namespace tareplay {

    class TaDemoCompiler: public QObject
    {
    public:
        TaDemoCompiler(QString demoPathTemplate, QHostAddress addr, quint16 port);
        ~TaDemoCompiler();

    private:

        struct UserContext
        {
            UserContext() { }
            UserContext(QTcpSocket* socket);

            quint32 gameId;
            quint32 playerDpId;
            QString playerPublicAddr;
            QSharedPointer<QDataStream> dataStream;
            gpgnet::GpgNetParse gpgNetParser;
            GamePlayerMessage gamePlayerInfo;
            int gamePlayerNumber;   // 1..10
        };

        static const quint32 GAME_EXPIRY_TICKS = 60;
        struct GameContext
        {
            GameContext();

            quint32 gameId;
            GameInfoMessage header;
            QMap<quint32, QSharedPointer<UserContext> > players;    // keyed by Dplay ID
            QVector<quint32> playersLockedIn;                       // those that actually progressed to loading
            QMap<QPair<quint8, quint32>, QByteArray> unitData;      // keyed by sub,id

            std::shared_ptr<std::ostream> demoCompilation;
            QString tempFileName;
            QString finalFileName;
            int expiryCountdown;    // continuing messages from players keep this counter from expiring
        };

        void onNewConnection();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        void onReadyRead();
        void closeExpiredGames();
        void timerEvent(QTimerEvent* event);

        std::shared_ptr<std::ostream> commitHeaders(const GameContext& game, QString filename);
        void commitMove(const GameContext &, int playerNumber, const GameMoveMessage &);

        QString m_demoPathTemplate;
        QTcpServer m_tcpServer;
        QMap<QTcpSocket*, QSharedPointer<UserContext> > m_players;
        QMap<quint32, GameContext> m_games;
    };

}
