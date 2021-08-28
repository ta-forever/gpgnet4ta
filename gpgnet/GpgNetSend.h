#pragma once

#include <QtCore/qdatastream.h>

namespace gpgnet
{

    class GpgNetSend
    {
        QDataStream& m_os;

    public:
        GpgNetSend(QDataStream& os);

        void sendCommand(QString command, int argumentCount);
        void sendArgument(QByteArray arg);
        void sendArgument(int arg);
    };
}
