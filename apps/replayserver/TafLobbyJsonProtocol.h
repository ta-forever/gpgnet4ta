#pragma once

#include <QtCore/qdatastream.h>
#include <QtCore/qsharedpointer.h>
#include <QtCore/qtextcodec.h>

class TafLobbyJsonProtocol
{
public:
    TafLobbyJsonProtocol(QIODevice* ioDevice);

    class DataNotReady : public std::exception
    { };

    void sendJson(const QJsonObject& json);
    void receiveJson(QJsonObject& json);

private:
    QDataStream m_dataStream;
    QSharedPointer<QTextEncoder> m_textEncoder;
    QSharedPointer<QTextDecoder> m_textDecoder;
};
