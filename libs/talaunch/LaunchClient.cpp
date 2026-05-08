#include "LaunchClient.h"
#include "tafgamestate.h"

#include <QtNetwork/qhostaddress.h>

using namespace talaunch;

LaunchClient::LaunchClient(QHostAddress addr, quint16 port) :
    m_serverAddress(addr),
    m_serverPort(port),
    m_state(State::CONNECTING),
    m_gameGuid("{99797420-F5F5-11CF-9827-00A0241496C8}"),
    m_gameId(0),
    m_playerName("BILLYIDOL"),
    m_gameAddress("127.0.0.1"),
    m_isHost(true),
    m_requireSearch(false),
    m_lastKillRingHead(0),
    m_lastTiebreakerWinnerDpid(0)

{
    // Custom-type registration for queued signal/slot delivery. Q_DECLARE_METATYPE alone
    // covers compile-time MetaTypeId resolution; qRegisterMetaType is what makes Qt's
    // queued-connection serialiser work across event loops.
    qRegisterMetaType<KillEventQt>("talaunch::KillEventQt");
    qRegisterMetaType<QVector<KillEventQt>>("QVector<talaunch::KillEventQt>");
    QObject::connect(&m_tcpSocket, &QTcpSocket::readyRead, this, &LaunchClient::onReadyReadTcp);
    QObject::connect(&m_tcpSocket, &QTcpSocket::stateChanged, this, &LaunchClient::onSocketStateChanged);
    if (connect(addr, port))
    {
        m_state = State::IDLE;
    }
}

bool LaunchClient::connect(QHostAddress addr, quint16 port)
{
    if (m_tcpSocket.waitForConnected(3))
    {
        return true;
    }

    qInfo() << "[LaunchClient::connect] connecting to server" << addr << "port" << port;
    for (int n = 0; n < 30; ++n)
    {
        m_tcpSocket.connectToHost(addr, port);
        if (m_tcpSocket.waitForConnected(1000))
        {
            qInfo() << "[LaunchClient::connect] connected to server";
            return true;
        }
    }
    if (!m_tcpSocket.waitForConnected(1000))
    {
        qWarning() << "[LaunchClient::connect] unable to connect to server";
        return false;
    }
    return true;
}

void LaunchClient::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    if (socketState == QAbstractSocket::UnconnectedState)
    {
        qWarning() << "[LaunchClient::onSocketStateChanged] socket disconnected! server crashed?";
        m_state = State::CONNECTING;
    }
}

void LaunchClient::setPlayerName(QString playerName)
{
    m_playerName = playerName;
}

void LaunchClient::setGameGuid(QString gameGuid)
{
    m_gameGuid = gameGuid;
}

void LaunchClient::setGameId(int gameId)
{
    m_gameId = gameId;
}

void LaunchClient::setAddress(QString address)
{
    m_gameAddress = address;
}

void LaunchClient::setIsHost(bool isHost)
{
    m_isHost = isHost;
}

void LaunchClient::setRequireSearch(bool requireSearch)
{
    m_requireSearch = requireSearch;
}

void LaunchClient::setSubmitGameFileHashesEndpoint(QString endpoint)
{
    m_submitGameFileHashesEndpoint = endpoint;
}

void LaunchClient::setSubmitGameFileHashesToken(QString token)
{
    m_submitGameFileHashesToken = token;
}

bool LaunchClient::failGameFileVersions(QString filename, QString reason)
{
    connect(m_serverAddress, m_serverPort);
    if (m_tcpSocket.waitForConnected(3))
    {
        QString message = QString("/failversion %1 %2)").arg(filename).arg(reason);
        qInfo() << "[LaunchClient::failGameFileVersions]" << message;
        m_tcpSocket.write(message.toUtf8());
        m_tcpSocket.flush();
        if (!m_tcpSocket.waitForReadyRead(30000))
        {
            qInfo() << "[LaunchClient::failGameFileVersions] Did not receive a reply from server";
            m_state = State::FAIL;
        }
        onReadyReadTcp();
    }
    else
    {
        qInfo() << "[LaunchClient::failGameFileVersions] cannot failGameFileVersions due to no connection to launch server";
    }

    return isApplicationRunning();
}

bool LaunchClient::startApplication()
{
    connect(m_serverAddress, m_serverPort);
    if (m_tcpSocket.waitForConnected(3))
    {
        static const char* cmd[2][2] = {{ "/join", "/searchjoin"},
                                        { "/host", "/host"}};

        QString args = QStringList({
            cmd[m_isHost][m_requireSearch], QString::number(m_gameId), m_gameGuid, m_playerName, m_gameAddress,
            m_submitGameFileHashesEndpoint, m_submitGameFileHashesToken
            }).join(' ');
        qInfo() << "[LaunchClient::startApplication]" << QString(args).replace(m_submitGameFileHashesToken, "*****");
        m_tcpSocket.write(args.toUtf8());
        m_tcpSocket.flush();
        if (!m_tcpSocket.waitForReadyRead(30000))
        {
            qInfo() << "[LaunchClient::startApplication] Did not receive a reply from server";
            m_state = State::FAIL;
        }
        onReadyReadTcp();
    }
    else
    {
        qInfo() << "[LaunchClient::startApplication] cannot startApplication due to no connection to launch server";
        m_state = State::CONNECTING;
    }

    return isApplicationRunning();
}

bool LaunchClient::isApplicationRunning()
{
    return m_state == State::RUNNING || m_state == State::LAUNCHED;
}

bool LaunchClient::isGameLaunched()
{
    return m_state == State::LAUNCHED;
}

void LaunchClient::onReadyReadTcp()
{
    if (m_tcpSocket.bytesAvailable() == 0)
    {
        return;
    }

    QByteArray data = m_tcpSocket.readAll();
    QStringList response = QString::fromUtf8(data).split(" ");

    // For PLAYER_STATUS, log only on STRUCTURAL change — compress unit count to 0/1
    // (zero / non-zero) so pure unit-count drift during active gameplay doesn't spam logs.
    // Other messages (state transitions etc.) always log.
    bool isPlayerStatus = !response.isEmpty() && response[0] == "PLAYER_STATUS";
    QString structuralKey;
    if (isPlayerStatus)
    {
        // Per-slot token format: "f0,...,f9:active:unitCount:team:propertyMask:dplayId"
        QStringList structural;
        structural << response[0];
        for (int i = 1; i < response.size(); ++i)
        {
            QStringList parts = response[i].split(':');
            if (parts.size() >= 6)
            {
                parts[2] = QString::number(parts[2].toInt() > 0 ? 1 : 0);
                structural << parts.join(':');
            }
            else
            {
                structural << response[i];
            }
        }
        structuralKey = structural.join(' ');
    }
    const bool isHeartbeatOrCountDrift = isPlayerStatus && structuralKey == m_lastPlayerStatusKey;
    if (!isHeartbeatOrCountDrift)
    {
        qInfo() << "[LaunchClient::onReadyReadTcp]" << response;
    }
    if (isPlayerStatus)
    {
        m_lastPlayerStatusKey = structuralKey;
    }

    State oldState = m_state;
    if (response[0] == "IDLE")
    {
        m_state = State::IDLE;
    }
    else if (response[0] == "RUNNING")
    {
        m_state = State::RUNNING;
    }
    else if (response[0] == "LAUNCHED")
    {
        m_state = State::LAUNCHED;
    }
    else if (response[0] == "FAIL")
    {
        m_state = State::FAIL;
    }
    else if (response[0] == "PLAYER_STATUS")
    {
        // Expected layout:
        //   [0]            = "PLAYER_STATUS"
        //   [1..10]        = 10 per-slot tokens "f0,...,f9:active:unitCount:team:propertyMask:dplayId"
        //   [11]           = "KILL_RING"   (optional — older producers may not send it)
        //   [12]           = killRingHead
        //   [13..28]       = 16 event tokens "wallClockMs:victim:killer:flags"
        if (response.size() >= 11)
        {
            QVector<int> allyFlags(100, 0), actives(10), unitCounts(10), allyTeams(10);
            QVector<int> propertyMasks(10), dplayIds(10);
            for (int i = 0; i < 10; i++) {
                QStringList tok = response[i+1].split(':');
                QStringList flags = tok.value(0).split(',');
                for (int j = 0; j < 10 && j < flags.size(); j++)
                    allyFlags[i*10 + j] = flags[j].toInt();
                actives[i]       = tok.value(1).toInt();
                unitCounts[i]    = tok.value(2).toInt();
                allyTeams[i]     = tok.value(3).toInt();
                propertyMasks[i] = tok.value(4).toInt();
                dplayIds[i]      = tok.value(5).toInt();
            }

            // Emit kill events FIRST. The PLAYER_STATUS unitCount=0 transition is what
            // triggers the elimination edge in GameMonitor2 (which can immediately latch
            // an end-game result). If kill events arrive AFTER, the dgun-tiebreaker can't
            // see them and falls back to last-man-standing. By emitting kill events first,
            // m_killEvents is populated before the elimination handler runs in the same
            // synchronous direct-connection invocation.
            //
            // Optional kill ring tail
            if (response.size() >= 13 + TAF_KILL_RING_SIZE && response[11] == "KILL_RING")
            {
                const quint32 head = response[12].toUInt();
                if (head != m_lastKillRingHead)
                {
                    QVector<KillEventQt> newEvents;
                    // Determine which ring slots are "newly visible" since last poll.
                    // The producer writes slot (head-1) % SIZE last, slot (head-2) % SIZE before
                    // that, etc. Anything older than max(head - SIZE, m_lastKillRingHead) is
                    // either lost (overwritten) or already seen.
                    const quint32 firstNewIdx = (head > m_lastKillRingHead + TAF_KILL_RING_SIZE)
                                                    ? (head - TAF_KILL_RING_SIZE)
                                                    : m_lastKillRingHead;
                    for (quint32 idx = firstNewIdx; idx < head; ++idx)
                    {
                        const int tokenPos = 13 + (idx % TAF_KILL_RING_SIZE);
                        if (tokenPos >= response.size()) break;
                        QStringList parts = response[tokenPos].split(':');
                        if (parts.size() < 4) continue;
                        KillEventQt ev;
                        ev.wallClockMs   = parts[0].toLongLong();
                        ev.victimDplayId = parts[1].toUInt();
                        ev.killerDplayId = parts[2].toUInt();
                        ev.flags         = static_cast<quint16>(parts[3].toUInt());
                        if (ev.wallClockMs == 0) continue;  // empty slot
                        newEvents.append(ev);
                    }
                    m_lastKillRingHead = head;
                    if (!newEvents.isEmpty())
                    {
                        qInfo() << "[LaunchClient::onReadyReadTcp] kill events received:" << newEvents.size();
                        emit killEventsReceived(newEvents);
                    }
                }

                // Optional TIEBREAKER tail: tdraw's tiebreaker decision (0 = undecided,
                // otherwise = winning player's dplayId). Token layout:
                //   [13 + TAF_KILL_RING_SIZE]     = "TIEBREAKER"
                //   [14 + TAF_KILL_RING_SIZE]     = winnerDplayId (uint32 as decimal string)
                const int tbHeaderPos = 13 + TAF_KILL_RING_SIZE;
                if (response.size() > tbHeaderPos + 1 && response[tbHeaderPos] == "TIEBREAKER")
                {
                    const quint32 winnerDpid = response[tbHeaderPos + 1].toUInt();
                    if (winnerDpid != 0u && winnerDpid != m_lastTiebreakerWinnerDpid)
                    {
                        m_lastTiebreakerWinnerDpid = winnerDpid;
                        qInfo() << "[LaunchClient::onReadyReadTcp] tiebreaker winner received:" << winnerDpid;
                        emit tiebreakerWinnerReceived(winnerDpid);
                    }
                }
            }

            // Emit player status AFTER kill events so the elimination handler downstream
            // sees the dgun-tiebreaker's m_killEvents already populated.
            emit playerStatusReceived(allyFlags, actives, unitCounts, allyTeams,
                                      propertyMasks, dplayIds);
        }
    }
}
