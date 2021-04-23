#include "GpgNetGameLauncher.h"
#include "tademo/Watchdog.h"
#include <QtCore/qcoreapplication.h>
#include "QtCore/qthread.h"

GpgNetGameLauncher::GpgNetGameLauncher(
    QString iniTemplate, QString iniTarget, QString guid, int playerLimit, bool lockOptions, int maxUnits,
    LaunchClient &launchClient, gpgnet::GpgNetSend &gpgNetSend) :
    m_iniTemplate(iniTemplate),
    m_iniTarget(iniTarget),
    m_guid(guid),
    m_playerLimit(playerLimit),
    m_lockOptions(lockOptions),
    m_maxUnits(maxUnits),
    m_launchClient(launchClient),
    m_gpgNetSend(gpgNetSend)
{
    m_gpgNetSend.gameState("Idle", "Idle");
    QObject::connect(&m_pollStillActiveTimer, &QTimer::timeout, this, &GpgNetGameLauncher::pollJdplayStillActive);
    QObject::connect(&m_quitCountResetTimer, &QTimer::timeout, this, &GpgNetGameLauncher::onResetQuitCount);
}

void GpgNetGameLauncher::onCreateLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal)
{
    try
    {
        TADemo::Watchdog wd("GpgNetGameLauncher::onCreateLobby", 100);
        qInfo() << "[GpgNetGameLauncher::handleCreateLobby] playername=" << playerName << "playerId=" << playerId;
        m_thisPlayerName = playerName;
        m_thisPlayerId = playerId;
        m_launchClient.setPlayerName(playerName);
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
        TADemo::Watchdog wd("GpgNetGameLauncher::pollJdplayStillActive", 100);
        if (!m_launchClient.isRunning())
        {
            qInfo() << "[GpgNetGameLauncher::pollJdplayStillActive] game stopped running. exit (or crash?)";
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

void GpgNetGameLauncher::onHostGame(QString mapName, QString mapDetails)
{
    try
    {
        TADemo::Watchdog wd("GpgNetGameLauncher::onHostGame", 1000);
        qInfo() << "[GpgNetGameLauncher::handleHostGame] mapname=" << mapName;
        QString sessionName = m_thisPlayerName + "'s Game";

        createTAInitFile(m_iniTemplate, m_iniTarget, sessionName, mapName, m_playerLimit, m_lockOptions, m_maxUnits);
        m_launchClient.setGameGuid(m_guid);
        m_launchClient.setAddress("127.0.0.1");
        m_launchClient.setIsHost(true);
 
        m_readyToLaunch = true;
        if (m_autoLaunch)
        {
            qInfo() << "[GpgNetGameLauncher::onHostGame] executing deferred launch";
            onLaunchGame();
        }

        m_gpgNetSend.playerOption(QString::number(m_thisPlayerId), "Team", 1);
        m_gpgNetSend.gameOption("Slots", m_playerLimit);
        qInfo() << "GameOption MapDetails" << mapDetails;
        m_gpgNetSend.gameOption("MapDetails", mapDetails);
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
        TADemo::Watchdog wd("GpgNetGameLauncher::onJoinGame", 1000);
        qInfo() << "[GpgNetGameLauncher::onJoinGame] playername=" << playerName << "playerId=" << playerId << "guid" << m_guid;

        const char* hostOn47624 = "127.0.0.1"; // game address ... or a GameReceiver proxy
        char hostip[257] = { 0 };
        std::strncpy(hostip, hostOn47624, 256);

        m_launchClient.setGameGuid(m_guid);
        m_launchClient.setAddress(hostip);
        m_launchClient.setIsHost(false);

        m_readyToLaunch = true;
        if (m_autoLaunch)
        {
            qInfo() << "[GpgNetGameLauncher::onJoinGame] executing deferred launch";
            onLaunchGame();
        }

        m_gpgNetSend.playerOption(QString::number(m_thisPlayerId), "Team", 1);
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
        TADemo::Watchdog wd("GpgNetGameLauncher::onExtendedMessage", 3000);
        qInfo() << "[GpgNetGameLauncher::onExtendedMessage]" << msg;
        if (msg == "/launch")
        {
            onLaunchGame();
        }
        else if (msg.startsWith("/map ") && msg.size()>5)
        {
            if (!m_isHost) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] Player is not host.  Cannot set map.  ignoring";
            }
            else if (m_alreadyLaunched) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] game already launched. ignoring";
            }
            else
            {
                QString mapDetails = msg.mid(5);
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] setting new map:" << mapDetails;
                m_gpgNetSend.gameOption("MapDetails", mapDetails);
            }
        }
        else if (msg == "/quit")
        {
            if (!m_launchClient.isRunning() || ++m_quitCount == 2)
            {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] terminating with m_quitCount=" << m_quitCount;
                qApp->quit();
            }
            else
            {
                // If game has been launched we're going to need need some more convincing to actually shut down
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] waiting for another /quit to confirm shutdown.  m_quitCount=" << m_quitCount;
                m_quitCountResetTimer.setSingleShot(true);
                m_quitCountResetTimer.setInterval(1000);
                m_quitCountResetTimer.start();
            }
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

void GpgNetGameLauncher::onResetQuitCount()
{
    try
    {
        TADemo::Watchdog wd("GpgNetGameLauncher::onResetQuitCount", 100);
        qInfo() << "[GpgNetGameLauncher::onResetQuitCount] resetting m_quitCount";
        m_quitCount = 0;
    }
    catch (std::exception &e)
    {
        qWarning() << "[GpgNetGameLauncher::onResetQuitCount] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetGameLauncher::onResetQuitCount] unknown exception";
    }
}

void GpgNetGameLauncher::onLaunchGame()
{
    if (m_alreadyLaunched)
    {
        qInfo() << "[GpgNetGameLauncher::onLaunchGame] game already launched. ignoring";
        return;
    }

    if (!m_readyToLaunch)
    {
        qInfo() << "[GpgNetGameLauncher::onLaunchGame] game not ready to launch. deferring";
        m_autoLaunch = true;
        return;
    }

    qInfo() << "[GpgNetGameLauncher::onLaunchGame] m_launchClient.launch()";
    if (!m_launchClient.launch())
    {
        qWarning() << "[GpgNetGameLauncher::doLaunchGame] unable to launch game";
        return;
    }

    m_alreadyLaunched = true;
    m_pollStillActiveTimer.start(3000);

    m_gpgNetSend.gameState("Lobby", "Battleroom");
    emit gameLaunched();
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
    txt.replace("{playerlimit}", QString::number(std::max(2, std::min(playerLimit, 10))));
    txt.replace("{maxunits}", QString::number(std::max(20, std::min(maxUnits, 1500))));
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
