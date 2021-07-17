#include "GameSender.h"
#include "tademo/Watchdog.h"
#include <tademo/HexDump.h>
#include <sstream>

using namespace tafnet;

GameSender::GameSender(QHostAddress gameAddress, quint16 enumPort) :
    m_gameAddress(gameAddress),
    m_enumPort(enumPort),
    m_udpSocket(new QUdpSocket()),
    m_tcpPort(0),
    m_udpPort(0)
{ }

GameSender::~GameSender()
{ }

void GameSender::setTcpPort(quint16 port)
{
    m_tcpPort = port;
}

void GameSender::setUdpPort(quint16 port)
{
    m_udpPort = port;
}

void GameSender::setGameAddress(QHostAddress gameAddress)
{
    m_gameAddress = gameAddress;
}

bool GameSender::enumSessions(const char* data, int len)
{
    TADemo::Watchdog wd("[GameSender::enumSessions]", 100);
    m_enumSocket.connectToHost(m_gameAddress, m_enumPort);
    if (!m_enumSocket.waitForConnected(30))
    {
        return false;
    }

    m_enumSocket.write(data, len);
    m_enumSocket.flush();
    m_enumSocket.disconnectFromHost();
    return true;
}

bool GameSender::openTcpSocket(int timeoutMillisecond)
{
    TADemo::Watchdog wd("GameSender::openTcpSocket", 100+timeoutMillisecond);
    if (m_tcpPort > 0 && !m_tcpSocket.isOpen())
    {
        qInfo() << "[GameSender::openTcpSocket]" << m_gameAddress.toString() << ":" << m_tcpPort;
        m_tcpSocket.connectToHost(m_gameAddress, m_tcpPort);
        m_tcpSocket.waitForConnected(timeoutMillisecond);
    }
    return m_tcpSocket.isOpen();
}

void GameSender::sendTcpData(char* data, int len)
{
    TADemo::Watchdog wd("GameSender::sendTcpData", 600);
    if (!m_tcpSocket.isOpen())
    {
        openTcpSocket(500);
    }
    if (!m_tcpSocket.isOpen())
    {
        qWarning() << "[GameSender::sendTcpData] unable to open socket!";
    }
    m_tcpSocket.write(data, len);
    m_tcpSocket.flush();
}

void GameSender::sendUdpData(char* data, int len, quint16 port)
{
    TADemo::Watchdog wd("GameSender::sendUdpData", 100);
    static bool logged = false;

    if (port == 0)
    {
        port = m_udpPort;
    }

    if (port > 0)
    {
        if (!logged) qInfo() << "[GameSender::sendUdpData] sending to udp port" << port;
        m_udpSocket->writeDatagram(data, len, m_gameAddress, port);
        m_udpSocket->flush();
    }
    else if (m_tcpPort >= 2300 && m_tcpPort < 2350)
    {
        if (!logged) qWarning() << "[GameSender::sendUdpData] UDP port not known! Going scatter gun from port" << 2350 << "to" << m_tcpPort+50;
        for (quint16 port = 2350; port <= m_tcpPort+50; ++port)
        {
            m_udpSocket->writeDatagram(data, len, m_gameAddress, port);
            m_udpSocket->flush();
        }
    }
    else if (m_tcpPort > 0)
    {
        if (!logged) qWarning() << "[GameSender::sendUdpData] no information about udp port assignments available, and weird tcp port.  going tcp+50:" << m_tcpPort + 50;
        m_udpSocket->writeDatagram(data, len, m_gameAddress, m_tcpPort+50);
        m_udpSocket->flush();
    }
    else
    {
        if (!logged) qWarning() << "[GameSender::sendUdpData] no information about tcp or udp port assignments available!  Going scatter gun from 2350 to 2399";
        for (quint16 port = 2350; port < 2400; ++port)
        {
            m_udpSocket->writeDatagram(data, len, m_gameAddress, port);
            m_udpSocket->flush();
        }
    }
    logged = true;
}

QSharedPointer<QUdpSocket> GameSender::getUdpSocket()
{
    return m_udpSocket;
}
