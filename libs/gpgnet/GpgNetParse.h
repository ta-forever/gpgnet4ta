#pragma once

#include <QtCore/qdatastream.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtCore/qvector.h>

namespace gpgnet
{
    class GpgNetParse
    {
        class RecordReader
        {
            QByteArray m_buffer;
            int m_progress;
        public:
            RecordReader();
            void setSize(int size);
            virtual void reset();
            virtual const QByteArray& get(QDataStream& is);
        };

        class ByteRecordReader : public RecordReader
        {
        public:
            ByteRecordReader();
            quint8 getByte(QDataStream& is);
        };

        class IntRecordReader : public RecordReader
        {
        public:
            IntRecordReader();
            quint32 getInt(QDataStream& is);
        };

        class ByteArrayRecordReader : public RecordReader
        {
        public:
            virtual void reset();
            virtual const QByteArray& get(QDataStream& is);

        private:
            IntRecordReader m_size;
            RecordReader m_data;
        };

    public:
        class DataNotReady : public std::exception
        { };

        QVariantList GetCommand(QDataStream& is);
        void reset();

        ByteArrayRecordReader m_command;
        IntRecordReader m_numArgs;
        QVector<QSharedPointer<ByteRecordReader> > m_argTypes;
        QVector<QSharedPointer<RecordReader> > m_args;
    };
}
