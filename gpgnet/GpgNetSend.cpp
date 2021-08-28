#include "GpgNetSend.h"
#include <QtCore/qdebug.h>

namespace gpgnet
{
    GpgNetSend::GpgNetSend(QDataStream& os) :
        m_os(os)
    { }

    void GpgNetSend::sendCommand(QString command, int argumentCount)
    {
        m_os << command.toUtf8() << quint32(argumentCount);
    }

    void GpgNetSend::sendArgument(QByteArray arg)
    {
        m_os << quint8(1) << arg;
    }

    void GpgNetSend::sendArgument(int arg)
    {
        m_os << quint8(0) << quint32(arg);
    }
};
