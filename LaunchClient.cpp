#include "LaunchClient.h"

#include <QtNetwork/qhostaddress.h>


LaunchClient::LaunchClient(QHostAddress addr, quint16 port) :
    m_serverAddress(addr),
    m_serverPort(port),
    m_state(State::CONNECTING),
    m_gameGuid("{99797420-F5F5-11CF-9827-00A0241496C8}"),
    m_playerName("BILLYIDOL"),
    m_gameAddress("127.0.0.1"),
    m_isHost(true)

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

bool LaunchClient::launch()
{
    connect(m_serverAddress, m_serverPort);
    if (m_tcpSocket.waitForConnected(3))
    {
        QString args = QStringList({ m_isHost ? "/host" : "/join", m_gameGuid, m_playerName, m_gameAddress }).join(' ');
        qInfo() << "[LaunchClient::launch]" << args;
        m_tcpSocket.write(args.toUtf8());
        m_tcpSocket.flush();
        if (!m_tcpSocket.waitForReadyRead(10000))
        {
            qInfo() << "[LaunchClient::launch] Did not receive a reply from server";
            m_state = State::FAIL;
        }
        onReadyReadTcp();
    }
    else
    {
        qInfo() << "[LaunchClient::launch] cannot launch due to no connection to launch server";
        m_state = State::CONNECTING;
    }

    return LaunchClient::isRunning();
}

bool LaunchClient::isRunning()
{
    return m_state == State::RUNNING;
}

void LaunchClient::onReadyReadTcp()
{
    if (m_tcpSocket.bytesAvailable() == 0)
    {
        return;
    }

    QByteArray data = m_tcpSocket.readAll();
    QString response = QString::fromUtf8(data);
    qInfo() << "[LaunchClient::onReadyReadTcp]" << response;
    if (response == "IDLE")
    {
        m_state = State::IDLE;
    }
    else if (response == "RUNNING")
    {
        m_state = State::RUNNING;
    }
    else if (response == "FAIL")
    {
        m_state = State::FAIL;
    }
}
