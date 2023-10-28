#include "GpgNetGameLauncher.h"
#include "taflib/Watchdog.h"
#include <QtCore/qcoreapplication.h>
#include <QtCore/qfileinfo.h>
#include "QtCore/qthread.h"
#include <cstring>

GpgNetGameLauncher::GpgNetGameLauncher(
    QString iniTemplate, QString gamePath, QString iniTarget, QString guid, int playerLimit, bool lockOptions, int maxUnits,
    talaunch::LaunchClient &launchClient, gpgnet::GpgNetClient &gpgNetclient) :
    m_iniTemplate(iniTemplate),
    m_gamePath(gamePath),
    m_iniTarget(iniTarget),
    m_guid(guid),
    m_playerLimit(playerLimit),
    m_lockOptions(lockOptions),
    m_maxUnits(maxUnits),
    m_launchClient(launchClient),
    m_gpgNetClient(gpgNetclient)
{
    m_gpgNetClient.sendGameState("Idle", "Idle");
    QObject::connect(&m_pollStillActiveTimer, &QTimer::timeout, this, &GpgNetGameLauncher::pollJdplayStillActive);
    QObject::connect(&m_quitCountResetTimer, &QTimer::timeout, this, &GpgNetGameLauncher::onResetQuitCount);
}

void GpgNetGameLauncher::onCreateLobby(int protocol, int localPort, QString playerName, QString, int playerId, int natTraversal)
{
    try
    {
        taflib::Watchdog wd("GpgNetGameLauncher::onCreateLobby", 100);
        qInfo() << "[GpgNetGameLauncher::handleCreateLobby] playername=" << playerName << "playerId=" << playerId;
        m_thisPlayerName = playerName;
        m_thisPlayerId = playerId;
        m_launchClient.setPlayerName(playerName);
        m_gpgNetClient.sendGameState("Lobby", "Staging");
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
        taflib::Watchdog wd("GpgNetGameLauncher::pollJdplayStillActive", 100);
        if (!m_launchClient.isApplicationRunning())
        {
            qInfo() << "[GpgNetGameLauncher::pollJdplayStillActive] game stopped running. exit (or crash?)";
            m_pollStillActiveTimer.stop();
            m_gpgNetClient.sendGameEnded();
            emit applicationTerminated();
        }
        else if (m_isHost && !m_alreadyLaunched && m_launchClient.isGameLaunched())
        {
            qInfo() << "[GpgNetGameLauncher::pollJdplayStillActive] game launch detected";
            m_alreadyLaunched = true;
            m_gpgNetClient.sendGameState("Launching", "Launching");
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
        taflib::Watchdog wd("GpgNetGameLauncher::onHostGame", 1000);
        qInfo() << "[GpgNetGameLauncher::handleHostGame] mapname=" << mapName;

        m_mapName = mapName;
        m_launchClient.setGameGuid(m_guid);
        m_launchClient.setAddress("127.0.0.1");
        m_launchClient.setIsHost(true);
 
        m_readyToStart = true;
        if (m_autoStart)
        {
            qInfo() << "[GpgNetGameLauncher::onHostGame] executing deferred launch";
            onStartApplication();
        }

        m_gpgNetClient.sendPlayerOption(QString::number(m_thisPlayerId), "Team", 1);
        m_gpgNetClient.sendGameOption("Slots", m_playerLimit);
        qInfo() << "GameOption MapDetails" << mapDetails;
        m_gpgNetClient.sendGameOption("MapDetails", mapDetails);
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

void GpgNetGameLauncher::onJoinGame(QString host, QString playerName, QString, int playerId)
{
    try
    {
        taflib::Watchdog wd("GpgNetGameLauncher::onJoinGame", 1000);
        qInfo() << "[GpgNetGameLauncher::onJoinGame] playername=" << playerName << "playerId=" << playerId << "guid" << m_guid;

        const char* hostOn47624 = "127.0.0.1"; // game address ... or a GameReceiver proxy
        char hostip[257] = { 0 };
        std::strncpy(hostip, hostOn47624, 256);

        m_launchClient.setGameGuid(m_guid);
        m_launchClient.setAddress(hostip);
        m_launchClient.setIsHost(false);

        m_readyToStart = true;
        if (m_autoStart)
        {
            qInfo() << "[GpgNetGameLauncher::onJoinGame] executing deferred launch";
            onStartApplication();
        }

        m_gpgNetClient.sendPlayerOption(QString::number(m_thisPlayerId), "Team", 1);
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
        taflib::Watchdog wd("GpgNetGameLauncher::onExtendedMessage", 3000);
        if (msg.startsWith("/launch"))
        {
            if (!msg.endsWith("/launch"))
            {
                // a specific join order has been requested
                m_randomPositions = false;
            }
            onStartApplication();
        }
        else if (msg.startsWith("/map ") && msg.size()>5)
        {
            if (!m_isHost) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] Player is not host.  Cannot set map.  ignoring";
            }
            else if (m_alreadyStarted) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] game already launched. ignoring";
            }
            else
            {
                QString mapDetails = msg.mid(5);
                m_mapName = mapDetails.split(QChar(0x1f))[0];
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] setting new map '" << m_mapName << "'. details:" << mapDetails;
                m_gpgNetClient.sendGameOption("MapDetails", mapDetails);
            }
        }
        else if (msg.startsWith("/rating_type ") && msg.size() > 13)
        {
            if (!m_isHost) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] Player is not host.  Cannot set rating type.  ignoring";
            }
            else if (m_alreadyStarted) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] game already launched. ignoring";
            }
            else
            {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] setting rating type";
                m_gpgNetClient.sendGameOption("RatingType", msg.mid(13));
            }
        }
        else if (msg.startsWith("/replay_delay_seconds ") && msg.size() > 22)
        {
            if (!m_isHost) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] Player is not host.  Cannot set replay delay.  ignoring";
            }
            else if (m_alreadyStarted) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] game already launched. ignoring";
            }
            else
            {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] setting replay delay";
                m_gpgNetClient.sendGameOption("ReplayDelaySeconds", msg.mid(22));
            }
        }
        else if (msg.startsWith("/title ") && msg.size() > 6)
        {
            if (!m_isHost) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] Player is not host.  Cannot set title.  ignoring";
            }
            else if (m_alreadyStarted) {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] game already launched. ignoring";
            }
            else
            {
                qInfo() << "[GpgNetGameLauncher::onExtendedMessage] setting title";
                m_gpgNetClient.sendGameOption("Title", msg.mid(6));
            }
        }
        else if (msg == "/quit")
        {
            if (!m_launchClient.isApplicationRunning() || ++m_quitCount == 2)
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
        qWarning() << "[GpgNetGameLauncher::onExtendedMessage] exception" << e.what();
    }
    catch (...)
    {
        qWarning() << "[GpgNetGameLauncher::onExtendedMessage] unknown exception";
    }
}

void GpgNetGameLauncher::onResetQuitCount()
{
    try
    {
        taflib::Watchdog wd("GpgNetGameLauncher::onResetQuitCount", 100);
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

void GpgNetGameLauncher::onStartApplication()
{
    if (m_alreadyStarted)
    {
        qInfo() << "[GpgNetGameLauncher::onStartApplication] game already launched. ignoring";
        return;
    }

    if (!m_readyToStart)
    {
        qInfo() << "[GpgNetGameLauncher::onStartApplication] game not ready to launch. deferring";
        m_autoStart = true;
        return;
    }

    QString sessionName = m_thisPlayerName + "'s Game";
    createTAInitFile(m_iniTemplate, m_iniTarget, sessionName, m_mapName, m_playerLimit, m_lockOptions, m_maxUnits, m_randomPositions);
    copyOnlineDll(m_gamePath + "/online.dll");
    m_alreadyLaunched = false;

    qInfo() << "[GpgNetGameLauncher::onStartApplication] m_launchClient.launch()";
    if (!m_launchClient.startApplication())
    {
        qWarning() << "[GpgNetGameLauncher::doLaunchGame] unable to launch game";
        return;
    }

    m_alreadyStarted = true;
    m_pollStillActiveTimer.start(1000);

    m_gpgNetClient.sendGameState("Lobby", "Battleroom");
    emit applicationStarted();
    // next we expect to receive a call to our onLaunchClientStateChanged slot
}

void GpgNetGameLauncher::createTAInitFile(QString tmplateFilename, QString iniFilename, QString session, QString mission, int playerLimit, bool lockOptions, int maxUnits, bool randomPositions)
{
    qInfo() << "[GpgNetGameLauncher::createTAInitFile] Loading ta ini template:" << tmplateFilename;
    QFile tmplt(tmplateFilename);
    if (!tmplt.open(QFile::ReadOnly | QFile::Text))
    {
        qWarning() << "GpgNetGameLauncher::createTAInitFile] unable to open template file";
        return;
    }

    QTextStream in(&tmplt);
    QString txt = in.readAll();
    txt.replace("{session}", session);
    txt.replace("{mission}", mission);
    txt.replace("{playerlimit}", QString::number(std::max(2, std::min(playerLimit, 10))));
    txt.replace("{maxunits}", QString::number(std::max(20, std::min(maxUnits, 1500))));
    txt.replace("{lockoptions}", lockOptions ? "1" : "0");
    txt.replace("{location}", randomPositions ? "2" : "1");

    qInfo() << "[GpgNetGameLauncher::createTAInitFile] saving ta ini:" << iniFilename;
    QFile ini(iniFilename);
    if (!ini.open(QFile::WriteOnly | QFile::Text))
    {
        return;
    }

    QTextStream out(&ini);
    ini.write(txt.toUtf8());
}

void GpgNetGameLauncher::copyOnlineDll(QString target)
{
    QFileInfo fileInfo(target);
    if (!fileInfo.exists()) {
      qInfo() << "[GpgNetGameLauncher::copyOnlineDll] copying online.dll to" << target;
      QFile::copy("online.dll", target);
    }
    if (!fileInfo.exists()) {
      qWarning() << "[GpgNetGameLauncher::copyOnlineDll] copy online.dll failed!";
    }
}
