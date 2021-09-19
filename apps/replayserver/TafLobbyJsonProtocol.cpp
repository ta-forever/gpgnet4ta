#include "TafLobbyJsonProtocol.h"

#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>

TafLobbyJsonProtocol::TafLobbyJsonProtocol(QIODevice* ioDevice) :
    m_dataStream(ioDevice)
{
    QTextCodec* codec = QTextCodec::codecForName("UTF-16BE");
    if (codec == NULL)
    {
        throw std::runtime_error("[TafLobbyJsonProtocol::TafLobbyJsonProtocol] unable to retrive UTF-16BE QTextCodec!");
    }
    m_textEncoder.reset(codec->makeEncoder(QTextCodec::IgnoreHeader));
    m_textDecoder.reset(codec->makeDecoder(QTextCodec::IgnoreHeader));
}

void TafLobbyJsonProtocol::sendJson(const QJsonObject& json)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds << m_textEncoder->fromUnicode(QJsonDocument(json).toJson(QJsonDocument::Compact));
    m_dataStream << bytes;
}

void TafLobbyJsonProtocol::receiveJson(QJsonObject& json)
{
    QByteArray bytes1;
    {
        m_dataStream.startTransaction();
        m_dataStream >> bytes1;
        if (!m_dataStream.commitTransaction())
        {
            throw DataNotReady();
        }
    }

    QByteArray bytes2;
    {
        QDataStream ds(&bytes1, QIODevice::ReadOnly);
        ds >> bytes2;
    }

    QJsonDocument doc = QJsonDocument::fromJson(m_textDecoder->toUnicode(bytes2).toUtf8());
    json = doc.object();
}
