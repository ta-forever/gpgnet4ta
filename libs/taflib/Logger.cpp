#include "Logger.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qthread.h>
#include <iostream>

using namespace taflib;

Logger::Logger(const std::string& filename, Verbosity verbosity) :
    m_verbosity(verbosity)
{
    if (filename.empty())
    {
        m_ostream = &std::cout;
    }
    else
    {
        m_optionalLogFile.reset(new std::ofstream(filename, std::iostream::out | std::iostream::app));
        m_ostream = m_optionalLogFile.get();
    }

    QString datetime = QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss");
    ostream() << std::endl;
    ostream() << datetime.toStdString() << " -------------- BEGIN LOG --------------" << std::endl;
}

std::ostream& Logger::ostream()
{
    return *m_ostream;
}


void Logger::Initialise(const std::string& filename, Verbosity level)
{
    m_instance.reset(new Logger(filename, level));
}

Logger* Logger::Get()
{
    return m_instance.get();
}

void Logger::Log(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Logger::Get()->LogToFile(type, context, msg);
}

void Logger::LogToFile(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    switch (type) {
    case QtDebugMsg:
        if (m_verbosity >= Verbosity::DEBUG)
        {
            QString datetime = QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss");
            ostream() << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Debug] " << msg.toStdString() << std::endl;
        }
        break;
    case QtInfoMsg:
        if (m_verbosity >= Verbosity::INFO)
        {
            QString datetime = QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss");
            ostream() << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Info] " << msg.toStdString() << std::endl;
        }
        break;
    case QtCriticalMsg:
        if (m_verbosity >= Verbosity::CRITICAL)
        {
            QString datetime = QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss");
            ostream() << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Critical] " << msg.toStdString() << std::endl;
        }
        break;
    case QtWarningMsg:
        if (m_verbosity >= Verbosity::WARNING)
        {
            QString datetime = QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss");
            ostream() << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Warning] " << msg.toStdString() << std::endl;
        }
        break;
    case QtFatalMsg:
        if (m_verbosity >= Verbosity::FATAL)
        {
            QString datetime = QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss");
            ostream() << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Fatal] " << msg.toStdString() << std::endl;
        }
        abort();
    }
}

std::shared_ptr<Logger> Logger::m_instance;
