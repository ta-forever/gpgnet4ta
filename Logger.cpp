#include "Logger.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qthread.h>

Logger::Logger(const std::string& filename, Verbosity verbosity) :
    m_logfile(filename, std::iostream::out | std::iostream::app),
    m_verbosity(verbosity)
{
    QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
    m_logfile << std::endl;
    m_logfile << datetime.toStdString() << " -------------- BEGIN LOG --------------" << std::endl;
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
            QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
            m_logfile << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Debug] " << msg.toStdString() << std::endl;
        }
        break;
    case QtInfoMsg:
        if (m_verbosity >= Verbosity::INFO)
        {
            QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
            m_logfile << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Info] " << msg.toStdString() << std::endl;
        }
        break;
    case QtCriticalMsg:
        if (m_verbosity >= Verbosity::CRITICAL)
        {
            QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
            m_logfile << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Critical] " << msg.toStdString() << std::endl;
        }
        break;
    case QtWarningMsg:
        if (m_verbosity >= Verbosity::WARNING)
        {
            QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
            m_logfile << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Warning] " << msg.toStdString() << std::endl;
        }
        break;
    case QtFatalMsg:
        if (m_verbosity >= Verbosity::FATAL)
        {
            QString datetime = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss");
            m_logfile << datetime.toStdString() << " [" << QThread::currentThreadId() << ":Fatal] " << msg.toStdString() << std::endl;
        }
        abort();
    }
}

std::shared_ptr<Logger> Logger::m_instance;
