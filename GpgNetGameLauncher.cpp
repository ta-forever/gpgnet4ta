#include "GpgNetGameLauncher.h"
#include <QtCore/qcoreapplication.h>
#include "QtCore/qthread.h"

GpgNetGameLauncher::GpgNetGameLauncher(
    QString iniTemplate, QString iniTarget, QString guid, int playerLimit, bool lockOptions, int maxUnits,
    JDPlay &jdplay, gpgnet::GpgNetSend &gpgNetSend) :
    m_iniTemplate(iniTemplate),
    m_iniTarget(iniTarget),
    m_guid(guid),
    m_playerLimit(playerLimit),
    m_lockOptions(lockOptions),
    m_maxUnits(maxUnits),
    m_jdplay(jdplay),
    m_gpgNetSend(gpgNetSend)
{
    m_gpgNetSend.gameState("Idle", "Idle");
    QObject::connect(&m_pollStillActiveTimer, &QTimer::timeout, this, &GpgNetGameLauncher::pollJdplayStillActive);
}

void GpgNetGameLauncher::onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal)
{
    try
    {
        qInfo() << "[GpgNetGameLauncher::handleCreateLobby] playername=" << playerName << "playerId=" << playerId;
        m_thisPlayerName = playerName;
        m_thisPlayerId = playerId;
        m_jdplay.updatePlayerName(playerName.toStdString().c_str());
        m_gpgNetSend.gameState("Lobby", "Staging");
    }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetGameLauncher::onCreateLobby] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetGameLauncher::onCreateLobby] unknown exception";
    }
}

void GpgNetGameLauncher::pollJdplayStillActive()
{
    try
    {
        if (!m_jdplay.pollStillActive())
        {
            qInfo() << "[GpgNetGameLauncher::pollJdplayStillActive] game stopped running. exit (or crash?)";
            m_jdplay.releaseDirectPlay();
            m_pollStillActiveTimer.stop();
            m_gpgNetSend.gameEnded();
            emit gameTerminated();
        }
    }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetGameLauncher::pollJdplayStillActive] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetGameLauncher::pollJdplayStillActive] unknown exception";
    }
}

void GpgNetGameLauncher::onHostGame(QString mapName)
{
    try
    {
        qInfo() << "[GpgNetGameLauncher::handleHostGame] mapname=" << mapName;
        QString sessionName = m_thisPlayerName + "'s Game";
        createTAInitFile(m_iniTemplate, m_iniTarget, sessionName, mapName, m_playerLimit, m_lockOptions, m_maxUnits);
        if (!m_jdplay.initialize(m_guid.toStdString().c_str(), "127.0.0.1", true, 10))
        {
            qWarning() << "[GpgNetGameLauncher::handleHostGame] unable to initialise dplay";
            emit gameFailedToLaunch();
            return;
        }

        m_readyToLaunch = true;
        if (m_autoLaunch)
        {
            qInfo() << "[GpgNetGameLauncher::onHostGame] executing deferred launch";
            onLaunchGame();
        }

        m_gpgNetSend.playerOption(QString::number(m_thisPlayerId), "Color", 1);
        m_gpgNetSend.gameOption("Slots", m_playerLimit);
        m_isHost = true;
    }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetGameLauncher::onHostGame] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetGameLauncher::onHostGame] unknown exception";
    }
}

void GpgNetGameLauncher::onJoinGame(QString host, QString playerName, int playerId)
{
    try
    {
        qInfo() << "[GpgNetGameLauncher::handleJoinGame] playername=" << playerName << "playerId=" << playerId;
        const char* hostOn47624 = "127.0.0.1"; // game address ... or a GameReceiver proxy

        char hostip[257] = { 0 };
        std::strncpy(hostip, hostOn47624, 256);

        qInfo() << "[GpgNetGameLauncher::handleJoinGame] jdplay.initialize(join):" << m_guid << hostip;
        if (!m_jdplay.initialize(m_guid.toStdString().c_str(), hostip, false, m_playerLimit))
        {
            qWarning() << "[GpgNetGameLauncher::handleJoinGame] unable to initialise dplay";
            emit gameFailedToLaunch();
            return;
        }

        m_readyToLaunch = true;
        if (m_autoLaunch)
        {
            qInfo() << "[GpgNetGameLauncher::onJoinGame] executing deferred launch";
            onLaunchGame();
        }

        m_gpgNetSend.playerOption(QString::number(m_thisPlayerId), "Color", 1);
    }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetGameLauncher::onJoinGame] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetGameLauncher::onJoinGame] unknown exception";
    }
}

void GpgNetGameLauncher::onExtendedMessage(QString msg)
{
    try
    {
        if (msg == "/launch")
        {
            onLaunchGame();
        }
        else if (msg == "/quit")
        {
            qApp->quit();
        }
    }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetGameLauncher::onLaunchGame] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetGameLauncher::onLaunchGame] unknown exception";
    }
}

void GpgNetGameLauncher::onLaunchGame()
{
    if (m_alreadyLaunched)
    {
        qInfo() << "[GpgNetGameLauncher::doLaunchGame] jdplay already launched. ignoring";
        return;
    }

    if (!m_readyToLaunch)
    {
        qInfo() << "[GpgNetGameLauncher::doLaunchGame] jdplay not ready to launch. deferring";
        m_autoLaunch = true;
        return;
    }

    qInfo() << "[GpgNetGameLauncher::doLaunchGame] jdplay.launch(host)";
    if (!m_jdplay.launch(true))
    {
        qWarning() << "[GpgNetGameLauncher::doLaunchGame] unable to launch game";
        emit gameFailedToLaunch();
        return;
    }
    m_alreadyLaunched = true;
    m_pollStillActiveTimer.start(3000);

    // give TA a bit of time to start up since there seems to be a race condition on simultaneous host/join
    // @todo when change to reporter.dll we'll be able to detect existance of battleroom and move this gameState notification into GameMonitor
    QThread::msleep(300);
    m_gpgNetSend.gameState("Lobby", "Battleroom");
    // from here on, game state is not driven by GpgNetGameLauncher but instead is inferred by GameMonitor
}

void GpgNetGameLauncher::createTAInitFile(QString tmplateFilename, QString iniFilename, QString session, QString mission, int playerLimit, bool lockOptions, int maxUnits)
{
    qInfo() << "[GpgNetGameLauncher::createTAInitFile] Loading ta ini template:" << tmplateFilename;
    QFile tmplt(tmplateFilename);
    if (!tmplt.open(QFile::ReadOnly | QFile::Text))
    {
        return;
    }

    QTextStream in(&tmplt);
    QString txt = in.readAll();
    txt.replace("{session}", session);
    txt.replace("{mission}", mission);
    txt.replace("{playerlimit}", QString::number(max(2, min(playerLimit, 10))));
    txt.replace("{maxunits}", QString::number(max(20, min(maxUnits, 1500))));
    txt.replace("{lockoptions}", lockOptions ? "1" : "0");

    qInfo() << "[GpgNetGameLauncher::createTAInitFile] saving ta ini:" << iniFilename;
    QFile ini(iniFilename);
    if (!ini.open(QFile::WriteOnly | QFile::Text))
    {
        return;
    }

    QTextStream out(&ini);
    ini.write(txt.toUtf8());
}
