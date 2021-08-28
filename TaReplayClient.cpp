#include "TaReplayClient.h"

#include "QtCore/qtemporaryfile.h"
#include "QtCore/qdir.h"

#include "gpgnet/GpgNetParse.h"
#include "tademo/HexDump.h"
#include "qlogging.h"

TaReplayClient::TaReplayClient(QHostAddress replayServerAddress, quint16 replayServerPort, quint32 tafGameId, quint32 position):
    m_replayServerAddress(replayServerAddress),
    m_replayServerPort(replayServerPort),
    m_tafGameId(tafGameId),
    m_position(position),
    m_socketStream(&m_tcpSocket),
    m_gpgNetSerialiser(m_socketStream),
    m_status(TaReplayServerStatus::CONNECTING)
{
    QString tempFileName;
    {
        QTemporaryFile tmpfile(QDir::tempPath() + QString("/taf-replay-%1").arg(tafGameId) + ".XXXXXX.tad");
        tmpfile.open();
        tempFileName = tmpfile.fileName();
        qInfo() << "[TaReplayClient::TaReplayClient] replay buffer file:" << tempFileName;
    }
    m_replayBufferOStream.open(tempFileName.toStdString(), std::ios::out | std::ios::binary);
    m_replayBufferIStream.open(tempFileName.toStdString(), std::ios::in | std::ios::binary);

    m_socketStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
    QObject::connect(&m_tcpSocket, &QTcpSocket::readyRead, this, &TaReplayClient::onReadyRead);
    QObject::connect(&m_tcpSocket, &QTcpSocket::stateChanged, this, &TaReplayClient::onSocketStateChanged);
    startTimer(3000);
}

TaReplayClient::~TaReplayClient()
{
}

void TaReplayClient::timerEvent(QTimerEvent* event)
{
    connect();
}

void TaReplayClient::connect()
{
    if (!m_tcpSocket.isOpen())
    {
        qInfo() << "[TaReplayClient::connect] connecting to replay server" << m_replayServerAddress << m_replayServerPort;
        m_tcpSocket.connectToHost(m_replayServerAddress, m_replayServerPort);
    }
}

void TaReplayClient::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    if (socketState == QAbstractSocket::UnconnectedState)
    {
        qWarning() << "[TaReplayClient::onSocketStateChanged] socket disconnected";
        m_status = TaReplayServerStatus::CONNECTING;
    }
    else if (socketState == QAbstractSocket::ConnectedState)
    {
        qInfo() << "[TaReplayClient::onSocketStateChanged] socket connected";
        sendSubscribe(m_tafGameId, m_position);
    }
}

void TaReplayClient::sendSubscribe(quint32 gameId, quint32 position)
{
    qInfo() << "[TaReplayClient::sendSubscribe] gameId,position" << gameId << position;
    m_gpgNetSerialiser.sendCommand(TaReplayServerSubscribe::ID, 2);
    m_gpgNetSerialiser.sendArgument(gameId);
    m_gpgNetSerialiser.sendArgument(position);
}

void TaReplayClient::onReadyRead()
{
    try
    {
        while (m_tcpSocket.bytesAvailable() > 0)
        {
            QVariantList command = m_gpgNetParser.GetCommand(m_socketStream);
            QString cmd = command[0].toString();

            if (cmd == TaReplayServerData::ID)
            {
                TaReplayServerData msg(command);
                if (msg.status == TaReplayServerStatus::OK)
                {
                    m_replayBufferOStream.write(msg.data.data(), msg.data.size());
                    m_position += msg.data.size();
                }
                else
                {
                    qWarning() << "[TaReplayClient::onReadyRead] server replied status" << int(msg.status);
                }
            }
            else
            {
                qWarning() << "[TaReplayClient::onReadyRead] unexpected message from replay server!" << cmd;
                continue;
            }
        }
        m_replayBufferOStream.flush();
    }
    catch (const gpgnet::GpgNetParse::DataNotReady &)
    {
        qInfo() << "[TaReplayClient::onReadyRead] waiting for more data";
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayClient::onReadyRead] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaReplayClient::onReadyRead] general exception:";
    }
}

TaReplayServerStatus TaReplayClient::getStatus() const
{
    return m_status;
}

std::istream* TaReplayClient::getReplayStream()
{
    return &m_replayBufferIStream;
}
