#include "GpgNetParse.h"

#include <QtCore/qsharedpointer.h>

#include <sstream>


using namespace gpgnet;

static const quint32 MAX_RECORD_SIZE = 10000u;
static const quint32 MAX_NUM_ARGS = 10u;

GpgNetParse::RecordReader::RecordReader():
    m_progress(0)
{ }

void GpgNetParse::RecordReader::setSize(int size)
{
    m_buffer.resize(size);
}

void GpgNetParse::RecordReader::reset()
{
    m_progress = 0;
}

const QByteArray& GpgNetParse::RecordReader::get(QDataStream& is)
{
    if (m_progress < m_buffer.size())
    {
        int bytesRead = is.readRawData(m_buffer.data() + m_progress, m_buffer.size() - m_progress);
        if (bytesRead >= 0)
        {
            m_progress += bytesRead;
        }
    }

    if (m_progress == m_buffer.size())
    {
        return m_buffer;
    }

    throw DataNotReady();
}

GpgNetParse::ByteRecordReader::ByteRecordReader()
{
    setSize(1);
}

quint8 GpgNetParse::ByteRecordReader::getByte(QDataStream& is)
{
    return *(const quint8*)get(is).data();
}

GpgNetParse::IntRecordReader::IntRecordReader()
{
    setSize(4);
}

quint32 GpgNetParse::IntRecordReader::getInt(QDataStream& is)
{
    return *(const quint32*)get(is).data();
}

void GpgNetParse::ByteArrayRecordReader::reset()
{
    m_size.reset();
    m_data.reset();
}

const QByteArray& GpgNetParse::ByteArrayRecordReader::get(QDataStream& is)
{
    quint32 size = m_size.getInt(is);
    if (size > MAX_RECORD_SIZE)
    {
        std::ostringstream ss;
        ss << "[GpgNetParse::ByteArrayRecordRecordReader] record exceeds MAX_RECORD_SIZE. size=" << size;
        throw std::runtime_error(ss.str());
    }
    m_data.setSize(size);
    return m_data.get(is);
}

QVariantList GpgNetParse::GetCommand(QDataStream& is)
{
    QVariantList commandAndArgs;

    const QByteArray& command = m_command.get(is);
    commandAndArgs.append(command);

    quint32 numArgs = m_numArgs.getInt(is);
    if (numArgs > MAX_NUM_ARGS)
    {
        std::ostringstream ss;
        ss << "[GpgNetParse::GetCommand] number of arguments exceeds MAX_NUM_ARGS. numArgs=" << numArgs;
        throw std::runtime_error("[GpgNetParse::GetCommand] number of arguments exceeds MAX_NUM_ARGS");
    }

    for (unsigned nArg = 0u; nArg < numArgs; ++nArg)
    {
        if (m_argTypes.size() == nArg)
        {
            m_argTypes.push_back(QSharedPointer<ByteRecordReader>(new ByteRecordReader()));
        }
        quint8 argType = m_argTypes[nArg]->getByte(is);
        if (argType == 0)
        {
            if (m_args.size() == nArg)
            {
                m_args.push_back(QSharedPointer<RecordReader>(new IntRecordReader()));
            }
            quint32 arg = *(const quint32*)m_args[nArg]->get(is).data();
            commandAndArgs.append(arg);
        }
        else if (argType == 1)
        {
            if (m_args.size() == nArg)
            {
                m_args.push_back(QSharedPointer<RecordReader>(new ByteArrayRecordReader()));
            }
            const QByteArray& arg = m_args[nArg]->get(is);
            commandAndArgs.append(arg);
        }
        else
        {
            throw std::runtime_error("unexpected argument type");
        }

    }

    reset();
    return commandAndArgs;
}

void GpgNetParse::reset()
{
    m_command.reset();
    m_numArgs.reset();
    m_argTypes.clear();
    m_args.clear();
}
