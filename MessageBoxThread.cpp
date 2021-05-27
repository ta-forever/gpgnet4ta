#include "MessageBoxThread.h"

#include <QtCore/qlogging.h>
#include <QtCore/qdebug.h>

#include <windows.h>

MessageBoxThread::MessageBoxThread()
{
    this->moveToThread(&m_thread);
    m_thread.start();
}


void MessageBoxThread::onMessage(QString title, QString content, unsigned int flags)
{
    try
    {
        std::string _title = title.toStdString();
        std::string _content = content.toStdString();
        MessageBox(NULL, _content.c_str(), _title.c_str(), flags);
    }
    catch (const std::exception & e)
    {
        qWarning() << "[MessageBoxThread::onMessageBox]" << e.what();
    }
    catch (...)
    {
        qWarning() << "[MessageBoxThread::onMessageBox]";
    }
}
