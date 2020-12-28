#include "GpgNetGameLauncher.h"

GpgNetGameLauncher::GpgNetGameLauncher(
    QString iniTemplate, QString iniTarget, QString guid, int playerLimit, bool lockOptions,
    JDPlay &jdplay, gpgnet::GpgNetSend &gpgNetSend) :
    m_iniTemplate(iniTemplate),
    m_iniTarget(iniTarget),
    m_guid(guid),
    m_playerLimit(playerLimit),
    m_lockOptions(lockOptions),
    m_jdplay(jdplay),
    m_gpgNetSend(gpgNetSend)
{
    m_gpgNetSend.gameState("Idle");
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
        m_gpgNetSend.gameState("Lobby");
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
        createTAInitFile(m_iniTemplate, m_iniTarget, sessionName, mapName, m_playerLimit, m_lockOptions);
        bool ret = m_jdplay.initialize(m_guid.toStdString().c_str(), "127.0.0.1", true, 10);
        if (!ret)
        {
            qWarning() << "[GpgNetGameLauncher::handleHostGame] unable to initialise dplay";
            emit gameFailedToLaunch();
            return;
        }
        qInfo() << "[GpgNetGameLauncher::handleHostGame] jdplay.launch(host)";
        ret = m_jdplay.launch(true);
        if (!ret)
        {
            qWarning() << "[GpgNetGameLauncher::handleHostGame] unable to launch game";
            emit gameFailedToLaunch();
            return;
        }

        m_pollStillActiveTimer.start(3000);
        m_gpgNetSend.playerOption(QString::number(m_thisPlayerId), "Color", 1);
        m_gpgNetSend.gameOption("Slots", m_playerLimit);
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
        bool ret = m_jdplay.initialize(m_guid.toStdString().c_str(), hostip, false, m_playerLimit);
        if (!ret)
        {
            qWarning() << "[GpgNetGameLauncher::handleJoinGame] unable to initialise dplay";
            emit gameFailedToLaunch();
            return;
        }

        qInfo() << "[GpgNetGameLauncher::handleJoinGame] jdplay.launch(join)";
        ret = m_jdplay.launch(true);
        if (!ret)
        {
            qWarning() << "[GpgNetGameLauncher::handleJoinGame] unable to launch game";
            m_jdplay.releaseDirectPlay();
            emit gameFailedToLaunch();
            return;
        }
        m_pollStillActiveTimer.start(3000);
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

void GpgNetGameLauncher::createTAInitFile(QString tmplateFilename, QString iniFilename, QString session, QString mission, int playerLimit, bool lockOptions)
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
