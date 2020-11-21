#include "GameSender.h"

#ifdef _DEBUG
#include <tademo/HexDump.h>
#endif

using namespace tafnet;

GameSender::GameSender(QHostAddress gameAddress, quint16 enumPort) :
    m_gameAddress(gameAddress),
    m_enumPort(enumPort),
    m_udpSocket(new QUdpSocket()),
    m_tcpPort(0),
    m_udpPort(0)
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
    qDebug() << "[GameSender::enumSessions]" << m_gameAddress.toString() << ":" << m_enumPort;
    m_enumSocket.writeDatagram(data, len, m_gameAddress, m_enumPort);
    m_enumSocket.flush();
    //m_enumSocket.connectToHost(m_gameAddress, m_enumPort);
    //m_enumSocket.waitForConnected(100);
    //m_enumSocket.write(data, len);
    //m_enumSocket.flush();
    //m_enumSocket.disconnectFromHost();
}

bool GameSender::openTcpSocket(int timeoutMillisecond)
{
    if (m_tcpPort > 0 && !m_tcpSocket.isOpen())
    {
        qDebug() << "[GameSender::openTcpSocket]" << m_gameAddress.toString() << ":" << m_tcpPort;
        m_tcpSocket.connectToHost(m_gameAddress, m_tcpPort);
        m_tcpSocket.waitForConnected(timeoutMillisecond);
    }
    return m_tcpSocket.isOpen();
}

void GameSender::sendTcpData(char* data, int len)
{
    if (!m_tcpSocket.isOpen())
    {
        openTcpSocket(3);
    }
#ifdef _DEBUG
    qDebug() << "[GameSender::sendTcpData]" << m_gameAddress.toString() << m_tcpPort;
    TADemo::HexDump(data, len, std::cout);
#endif
    m_tcpSocket.write(data, len);
    m_tcpSocket.flush();
}

void GameSender::closeTcpSocket()
{
    qDebug() << "[GameSender::closeTcpSocket]" << m_gameAddress.toString() << ":" << m_tcpPort;
    m_tcpSocket.disconnectFromHost();
    m_tcpSocket.close();
}

void GameSender::sendUdpData(char* data, int len)
{
    if (m_udpPort > 0)
    {
#ifdef _DEBUG
        qDebug() << "[GameSender::sendUdpData]" << m_gameAddress.toString() << ":" << m_udpPort;
        TADemo::HexDump(data, len, std::cout);
#endif
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
