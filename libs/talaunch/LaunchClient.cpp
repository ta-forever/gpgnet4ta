#include "LaunchClient.h"

#include <QtNetwork/qhostaddress.h>

using namespace talaunch;

LaunchClient::LaunchClient(QHostAddress addr, quint16 port) :
    m_serverAddress(addr),
    m_serverPort(port),
    m_state(State::CONNECTING),
    m_gameGuid("{99797420-F5F5-11CF-9827-00A0241496C8}"),
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

        QString args = QStringList({ cmd[m_isHost][m_requireSearch], m_gameGuid, m_playerName, m_gameAddress }).join(' ');
        qInfo() << "[LaunchClient::startApplication]" << args;
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
    qInfo() << "[LaunchClient::onReadyReadTcp]" << response;

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
}
