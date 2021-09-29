#pragma once

#include <QtCore/qqueue.h>
#include <QtNetwork/qtcpserver.h>


#include "gpgnet/GpgNetSend.h"

#include "TaReplayServerMessages.h"

namespace tareplay {

    class TaReplayServer : public QObject
    {
    public:
        TaReplayServer(QString demoPathTemplate, QHostAddress addr, quint16 port, quint16 delaySeconds, qint64 maxBytesPerUserPerSecond);
        ~TaReplayServer();

        // @param delaySeconds -ve to disable replay altogether
        void setGameInfo(quint32 gameId, int delaySeconds, QString state);

    private:

        struct GameInfo
        {
            quint32 gameId;
            int delaySeconds;
            QString state;

            // for purpose of monitoring file size versus time
            // not the same instance as UserContext so no need to restore file position
            QSharedPointer<std::istream> demoFile;

            // the last delaySeconds worth of file sizes in bytes;
            QSharedPointer<QQueue<int> > demoFileSizeLog;
        };

        struct UserContext
        {
            UserContext() { }
            UserContext(QTcpSocket* socket);

            quint32 gameId;
            QSharedPointer<QDataStream> userDataStream;
            QSharedPointer<gpgnet::GpgNetSend> userDataStreamProtol;
            QSharedPointer<gpgnet::GpgNetParse> gpgNetParser;
            QSharedPointer<std::istream> demoFile;
        };

        void sendData(UserContext &user, TaReplayServerStatus status, QByteArray data);

        void onNewConnection();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
        void onReadyRead();
        void timerEvent(QTimerEvent* event);
        void updateFileSizeLog(GameInfo& gameInfo);
        void serviceUser(UserContext& user);
        std::istream* findReplayFileForGame(quint32 gameId);

        QString m_demoPathTemplate;
        quint16 m_delaySeconds;
        qint64 m_maxBytesPerUserPerSecond;
        QTcpServer m_tcpServer;
        QMap<QTcpSocket*, QSharedPointer<UserContext> > m_users;
        QMap<quint32, GameInfo> m_gameInfo;
    };

}