#include "TaReplayClient.h"

#include "QtCore/qtemporaryfile.h"
#include "QtCore/qdir.h"

#include "gpgnet/GpgNetParse.h"
#include "taflib/HexDump.h"
#include "qlogging.h"

using namespace tareplay;

TaReplayClient::TaReplayClient(QString replayServerHostName, quint16 replayServerPort, quint32 tafGameId, quint32 position):
    m_replayServerHostName(replayServerHostName),
    m_replayServerPort(replayServerPort),
    m_tafGameId(tafGameId),
    m_position(position),
    m_socketStream(&m_tcpSocket),
    m_gpgNetSerialiser(m_socketStream),
    m_status(TaReplayServerStatus::CONNECTING)
{
    QTemporaryFile tmpfile(QDir::tempPath() + QString("/taf-replay-%1").arg(tafGameId) + ".XXXXXX.tad");
    tmpfile.open();
    m_tempFilename = tmpfile.fileName();
    qInfo() << "[TaReplayClient::TaReplayClient] replay buffer file:" << m_tempFilename;

    m_replayBufferOStream.open(m_tempFilename.toStdString(), std::ios::out | std::ios::binary);
    qInfo() << "[TaReplayClient::TaReplayClient] ostream opened. good,eof,fail,bad=" << m_replayBufferOStream.good() << m_replayBufferOStream.eof() << m_replayBufferOStream.fail() << m_replayBufferOStream.bad();

    m_replayBufferIStream.open(m_tempFilename.toStdString(), std::ios::in | std::ios::binary);
    qInfo() << "[TaReplayClient::TaReplayClient] istream opened. good,eof,fail,bad=" << m_replayBufferIStream.good() << m_replayBufferIStream.eof() << m_replayBufferIStream.fail() << m_replayBufferIStream.bad();

    m_socketStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
    QObject::connect(&m_tcpSocket, &QTcpSocket::readyRead, this, &TaReplayClient::onReadyRead);
    QObject::connect(&m_tcpSocket, &QTcpSocket::stateChanged, this, &TaReplayClient::onSocketStateChanged);
    startTimer(3000);
}

TaReplayClient::~TaReplayClient()
{
    m_replayBufferOStream.close();
    m_replayBufferIStream.close();
    QDir().remove(m_tempFilename);
}

void TaReplayClient::timerEvent(QTimerEvent* event)
{
    try
    {
        connect();
    }
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayClient::timerEvent] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaReplayClient::timerEvent] general exception:";
    }
}

void TaReplayClient::connect()
{
    if (!m_tcpSocket.isOpen())
    {
        qInfo() << "[TaReplayClient::connect] connecting to replay server" << m_replayServerHostName << m_replayServerPort;
        m_tcpSocket.connectToHost(m_replayServerHostName, m_replayServerPort);
    }
}

void TaReplayClient::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
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
    catch (const std::exception & e)
    {
        qWarning() << "[TaReplayClient::onSocketStateChanged] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[TaReplayClient::onSocketStateChanged] general exception:";
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
                else if (msg.status == TaReplayServerStatus::GAME_NOT_FOUND)
                {
                    emit gameNotFound();
                }
                else
                {
                    qWarning() << "[TaReplayClient::onReadyRead] server replied status" << int(msg.status);
                }
            }
            else
            {
                qWarning() << "[TaReplayClient::onReadyRead] unexpected message from replay server!" << cmd;
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
