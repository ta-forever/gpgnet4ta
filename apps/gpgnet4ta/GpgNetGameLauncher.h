#pragma once

#include <QtCore/qdebug.h>
#include <QtCore/qmap.h>
#include <QtCore/qsharedpointer.h>
#include <QtCore/qstring.h>
#include <QtCore/qset.h>
#include <QtCore/qfile.h>
#include <QtCore/qtimer.h>

#include "LaunchClient.h"
#include "gpgnet/GpgNetClient.h"

class GpgNetGameLauncher: public QObject
{
    Q_OBJECT

    const QString m_iniTemplate;
    const QString m_gamePath;
    const QString m_iniTarget;
    const QString m_guid;
    const int m_playerLimit;
    const bool m_lockOptions;
    const int m_maxUnits;
    talaunch::LaunchClient &m_launchClient;
    gpgnet::GpgNetClient &m_gpgNetClient;

    QString m_thisPlayerName;
    QString m_mapName;
    int m_thisPlayerId;
    QTimer m_pollStillActiveTimer;

    bool m_autoStart = false;           // "start" as in run the executable
    bool m_readyToStart = false;
    bool m_alreadyStarted = false;
    bool m_alreadyLaunched = false;     // "launched" as in progressed from battleroom
    bool m_isHost = false;
    bool m_randomPositions = true;
    int m_quitCount = 0;
    QTimer m_quitCountResetTimer;

    QMap<QString, QSet<qint64> > m_gameFileVersions;    // key: filename; value: list of permitted crc32
    bool m_enableGameFileVersionVerify;

public:
    GpgNetGameLauncher(
        QString iniTemplate, QString gamepath, QString iniTarget, QString guid, int playerLimit, bool lockOptions, int maxUnits,
        talaunch::LaunchClient &launchClient, gpgnet::GpgNetClient &gpgNetClient);

    void parseGameFileVersions(QString versions);
    void setEnableGameFileVersionVerify(bool enable);

    void onCreateLobby(int protocol, int localPort, QString playerName, QString, int playerId, int natTraversal);
    void onHostGame(QString mapName, QString mapDetails);
    void onJoinGame(QString host, QString playerName, QString, int playerId);

    void onExtendedMessage(QString msg);
    void onStartApplication();

signals:
    void applicationStarted();
    void applicationTerminated();

public slots:
    void pollJdplayStillActive();
    void onResetQuitCount();

private:
    static void createTAInitFile(QString tmplateFilename, QString iniFilename, QString session, QString mission, int playerLimit, bool lockOptions, int maxUnits, bool randomPositions);
    static void copyOnlineDll(QString gamePath);
    bool verifyGameFileVersions();
};
