#pragma once

#include <QtCore/qqueue.h>
#include <QtNetwork/qtcpserver.h>


#include "gpgnet/GpgNetSend.h"

#include "TaReplayServerMessages.h"

class TaReplayServer : public QObject
{
public:
    TaReplayServer(QString demoPathTemplate, QHostAddress addr, quint16 port, quint16 delaySeconds);
    ~TaReplayServer();

private:

    struct UserContext
    {
        UserContext() { }
        UserContext(QTcpSocket* socket);

        quint32 gameId;
        QSharedPointer<QDataStream> userDataStream;
        QSharedPointer<gpgnet::GpgNetSend> userDataStreamProtol;
        QSharedPointer<gpgnet::GpgNetParse> gpgNetParser;
        QSharedPointer<std::istream> demoFile;
        QQueue<int> demoFileSizeLog;        // file size over the last 'delaySeconds'
    };

    void sendData(UserContext &user, TaReplayServerStatus status, QByteArray data);

    void onNewConnection();
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);
    void onReadyRead();
    void timerEvent(QTimerEvent* event);
    void updateFileSizeLog(UserContext& user);
    void serviceUser(UserContext& user);

    QString m_demoPathTemplate;
    quint16 m_delaySeconds;
    QTcpServer m_tcpServer;
    QMap<QTcpSocket*, QSharedPointer<UserContext> > m_users;
};