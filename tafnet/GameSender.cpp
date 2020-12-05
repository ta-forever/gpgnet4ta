#include "GameSender.h"
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
    qInfo() << "[GameSender::enumSessions]" << m_gameAddress.toString() << ":" << m_enumPort;
    //TADemo::HexDump(data, len, std::cout);
    //m_enumSocket.writeDatagram(data, len, m_gameAddress, m_enumPort);
    //m_enumSocket.flush();
    m_enumSocket.connectToHost(m_gameAddress, m_enumPort);
    if (!m_enumSocket.waitForConnected(3000))
    {
        qWarning() << "[GameSender::enumSessions] unable to connect to enumeration socket!";
    }
    m_enumSocket.write(data, len);
    m_enumSocket.flush();
    m_enumSocket.disconnectFromHost();
}

bool GameSender::openTcpSocket(int timeoutMillisecond)
{
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
    if (!m_tcpSocket.isOpen())
    {
        openTcpSocket(500);
    }

    //qInfo() << "[GameSender::sendTcpData]" << m_tcpSocket.peerAddress().toString() << ":" << m_tcpSocket.peerPort();
    //TADemo::HexDump(data, len, std::cout);
    m_tcpSocket.write(data, len);
    m_tcpSocket.flush();
}

void GameSender::closeTcpSocket()
{
    qInfo() << "[GameSender::closeTcpSocket]" << m_tcpSocket.peerAddress().toString() << ":" << m_tcpSocket.peerPort();
    m_tcpSocket.disconnectFromHost();
    m_tcpSocket.close();
}

void GameSender::sendUdpData(char* data, int len)
{
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

void GameSender::resetGameConnection()
{
    m_tcpPort = 0;
    m_udpPort = 0;
    closeTcpSocket();
}
