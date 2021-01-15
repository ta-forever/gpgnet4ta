#include "GameSender.h"
#include "TADemo/Watchdog.h"
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

void GameSender::enumSessions(char* data, int len)
{
    TADemo::Watchdog wd("[GameSender::enumSessions]", 1000);
    for (int n = 0; n < 10; ++n)
    {
        qInfo() << "[GameSender::enumSessions] connecting" << m_gameAddress.toString() << ":" << m_enumPort;
        m_enumSocket.connectToHost(m_gameAddress, m_enumPort);
        if (m_enumSocket.waitForConnected(1000))
        {
            qInfo() << "[GameSender::enumSessions] connected ...";
            break;
        }
    }
    if (!m_enumSocket.waitForConnected(1000))
    {
        qWarning() << "[GameSender::enumSessions] unable to connect to enumeration socket!";
        return;
    }

    m_enumSocket.write(data, len);
    m_enumSocket.flush();
    m_enumSocket.disconnectFromHost();
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

void GameSender::sendUdpData(char* data, int len)
{
    TADemo::Watchdog wd("GameSender::sendUdpData", 100);
    if (m_udpPort > 0)
    {
        m_udpSocket->writeDatagram(data, len, m_gameAddress, m_udpPort);
        m_udpSocket->flush();
    }
}

QSharedPointer<QUdpSocket> GameSender::getUdpSocket()
{
    return m_udpSocket;
}
