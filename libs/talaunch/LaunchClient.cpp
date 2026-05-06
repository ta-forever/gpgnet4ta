#include "LaunchClient.h"

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
    m_requireSearch(false)

{ 
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
        // Expect: "PLAYER_STATUS" + 10 per-slot tokens = 11.
        if (response.size() == 11)
        {
            // each per-slot token is "f0,...,f9:active:unitCount:team:propertyMask:dplayId"
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
            emit playerStatusReceived(allyFlags, actives, unitCounts, allyTeams,
                                      propertyMasks, dplayIds);
        }
    }
}
