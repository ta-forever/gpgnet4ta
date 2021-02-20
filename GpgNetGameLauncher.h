#pragma once

#include <QtCore/qdebug.h>
#include <QtCore/qmap.h>
#include <QtCore/qsharedpointer.h>
#include <QtCore/qfile.h>
#include <QtCore/qtimer.h>

#include "jdplay/JDPlay.h"
#include "gpgnet/GpgNetSend.h"

class GpgNetGameLauncher: public QObject
{
    Q_OBJECT

    const QString m_iniTemplate;
    const QString m_iniTarget;
    const QString m_guid;
    const int m_playerLimit;
    const bool m_lockOptions;
    const int m_maxUnits;
    JDPlay &m_jdplay;
    gpgnet::GpgNetSend &m_gpgNetSend;

    QString m_thisPlayerName;
    int m_thisPlayerId;
    QTimer m_pollStillActiveTimer;
    bool m_readyToLaunch = false;
    bool m_alreadyLaunched = false;
    bool m_isHost = false;
    bool m_autoLaunch = false;
    int m_quitCount = 0;
    QTimer m_quitCountResetTimer;

public:
    GpgNetGameLauncher(
        QString iniTemplate, QString iniTarget, QString guid, int playerLimit, bool lockOptions, int maxUnits,
        JDPlay &jdplay, gpgnet::GpgNetSend &gpgNetSend);

    void onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal);
    void onHostGame(QString mapName, QString mapDetails);
    void onJoinGame(QString host, QString playerName, int playerId);

    void onExtendedMessage(QString msg);
    void onLaunchGame();

signals:
    void gameLaunched();
    void gameFailedToLaunch();
    void gameTerminated();

public slots:
    void pollJdplayStillActive();
    void onResetQuitCount();

private:
    static void createTAInitFile(QString tmplateFilename, QString iniFilename, QString session, QString mission, int playerLimit, bool lockOptions, int maxUnits);
};
