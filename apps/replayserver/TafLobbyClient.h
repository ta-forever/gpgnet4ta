#pragma once

#include "TafLobbyJsonProtocol.h"

#include "QtCore/qdatastream.h"
#include "QtNetwork/qtcpsocket.h"


struct TafLobbyPlayerInfo
{
    TafLobbyPlayerInfo();
    TafLobbyPlayerInfo(const QJsonObject& playerInfo);
    qint64 id;
    QString login;
};
Q_DECLARE_METATYPE(TafLobbyPlayerInfo)

struct TafLobbyGameInfo
{
    TafLobbyGameInfo();
    TafLobbyGameInfo(const QJsonObject& playerInfo);
    qint64 id;
    QString host;
    QString title;
    QString featuredMod;
    QString mapName;
    QString mapFilePath;    // "totala2.hpi/SHERWOOD/ead82fc5"
    QString gameType;
    QString ratingType;
    QString visibility;
    bool passwordProtected;
    QString state;
    int replayDelaySeconds;
    int numPlayers;
    int maxPlayers;
    QMap<QString, QStringList> teams;
};

Q_DECLARE_METATYPE(TafLobbyGameInfo)

class TafLobbyClient : public QObject
{
    Q_OBJECT

public:
    TafLobbyClient(QString userAgentName, QString userAgentVersion);
    ~TafLobbyClient();

    void connectToHost(QString hostName, quint16 port);
    void sendAskSession();
    void sendHello(qint64 session, QString uniqueId, QString localIp, QString login, QString password);
    void sendPong();

signals:
    void notice(QString style, QString text);
    void session(qint64 sessionId);
    void welcome(TafLobbyPlayerInfo playerInfo);
    void playerInfo(TafLobbyPlayerInfo playerInfo);
    void gameInfo(TafLobbyGameInfo gameInfo);

private:
    TafLobbyJsonProtocol m_protocol;

    void timerEvent(QTimerEvent* event);
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);
    void onReadyRead();

    const QString m_userAgentName;
    const QString m_userAgentVersion;

    QString m_hostName;
    quint16 m_port;

    QTcpSocket m_socket;
 
};
