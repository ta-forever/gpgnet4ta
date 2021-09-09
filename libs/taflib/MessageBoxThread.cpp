#include "MessageBoxThread.h"

#include <QtCore/qlogging.h>
#include <QtCore/qdebug.h>

#include <windows.h>

using namespace taflib;

MessageBoxThread::MessageBoxThread()
{
    this->moveToThread(&m_thread);
    m_thread.start();
}

MessageBoxThread::~MessageBoxThread()
{
    m_thread.quit();
    m_thread.wait(300);
}

void MessageBoxThread::onMessage(QString title, QString content, unsigned int flags)
{
    try
    {
        std::string _title = title.toStdString();
        std::string _content = content.toStdString();
        int result = MessageBox(NULL, _content.c_str(), _title.c_str(), flags);
        emit userAcknowledged(result);
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
